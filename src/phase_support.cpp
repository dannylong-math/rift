/**
 * \file
 * \brief Immutable phase-support value and view implementation.
 */

#include <algorithm>
#include <array>
#include <boost/serialization/array.hpp>  // IWYU pragma: keep
#include <boost/serialization/string.hpp> // IWYU pragma: keep
#include <boost/serialization/vector.hpp> // IWYU pragma: keep
#include <cstddef>
#include <cstdint>
#include <deal.II/base/geometry_info.h>
#include <deal.II/base/mpi.h>
#include <deal.II/base/mpi.templates.h>
#include <deal.II/base/types.h>
#include <deal.II/grid/cell_id.h>
#include <deal.II/grid/grid_tools.h>
#include <expected>
#include <format>
#include <iterator>
#include <limits>
#include <memory>
#include <mpi.h>
#include <optional>
#include <ranges>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/phase_support.hpp>
#include <rift/rift_context.hpp>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace rift {

namespace {

/** \brief Fixed-size rank-local summary used by the validation reductions. */
using PhaseSupportValidationRecord = std::array<std::uint64_t, 5>;

/** \brief Position of each field in a phase-support validation record. */
enum class PhaseSupportValidationField : std::uint8_t {
    dimension,
    has_phase_graph,
    has_mesh,
    mesh_snapshot_id,
    local_inputs_valid,
};

static_assert(std::tuple_size_v<PhaseSupportValidationRecord> ==
              static_cast<std::size_t>(PhaseSupportValidationField::local_inputs_valid) + 1);

/** \brief Convert a validation field to its fixed record index. */
[[nodiscard]] constexpr std::size_t field_index(const PhaseSupportValidationField field) noexcept
{
    return static_cast<std::size_t>(field);
}

/** \brief Compare cell IDs without invoking deal.II operations on an invalid ID. */
struct CellIdLess {
    [[nodiscard]] bool operator()(const dealii::CellId& left, const dealii::CellId& right) const noexcept
    {
        const auto left_coarse_cell = left.get_coarse_cell_id();
        const auto right_coarse_cell = right.get_coarse_cell_id();
        if (left_coarse_cell != right_coarse_cell) {
            return left_coarse_cell < right_coarse_cell;
        }
        if (left_coarse_cell == dealii::numbers::invalid_coarse_cell_id) {
            return false;
        }

        return std::ranges::lexicographical_compare(left.get_child_indices(), right.get_child_indices());
    }
};

/** \brief Compare cell IDs for equality without inspecting invalid child storage. */
[[nodiscard]] bool same_cell_id(const dealii::CellId& left, const dealii::CellId& right) noexcept
{
    if (left.get_coarse_cell_id() != right.get_coarse_cell_id()) {
        return false;
    }
    if (left.get_coarse_cell_id() == dealii::numbers::invalid_coarse_cell_id) {
        return true;
    }
    return std::ranges::equal(left.get_child_indices(), right.get_child_indices());
}

/** \brief Check the dimension-dependent parts of a caller-provided cell ID. */
template<int dim> [[nodiscard]] bool is_well_formed(const dealii::CellId& cell) noexcept
{
    if (cell.get_coarse_cell_id() == dealii::numbers::invalid_coarse_cell_id) {
        return false;
    }
    return std::ranges::all_of(cell.get_child_indices(), [](const std::uint8_t child) {
        return child < dealii::GeometryInfo<dim>::max_children_per_cell;
    });
}

/** \brief Format a valid cell ID or a safe placeholder for malformed input. */
template<int dim> [[nodiscard]] std::string format_cell_id(const dealii::CellId& cell)
{
    if (!is_well_formed<dim>(cell)) {
        return "<invalid CellId>";
    }
    return cell.to_string();
}

/** \brief Append one structured rank-local phase-support error. */
void add_error(PhaseSupportErrors& errors, const PhaseSupportErrorCode code, const unsigned int rank,
               std::optional<PhaseId> phase, std::optional<dealii::CellId> cell, std::string message)
{
    errors.push_back({.code = code, .rank = rank, .phase = phase, .cell = cell, .message = std::move(message)});
}

/** \brief Order public errors by their approved structured diagnostic fields. */
[[nodiscard]] bool error_less(const PhaseSupportError& left, const PhaseSupportError& right) noexcept
{
    const auto left_prefix =
        std::tuple{left.rank, static_cast<std::uint8_t>(left.code), left.phase.has_value(),
                   left.phase.has_value() ? left.phase->value() : std::uint32_t{0}, left.cell.has_value()};
    const auto right_prefix =
        std::tuple{right.rank, static_cast<std::uint8_t>(right.code), right.phase.has_value(),
                   right.phase.has_value() ? right.phase->value() : std::uint32_t{0}, right.cell.has_value()};
    if (left_prefix != right_prefix) {
        return left_prefix < right_prefix;
    }
    if (!left.cell.has_value()) {
        return false;
    }
    // Equal prefixes guarantee that the right-hand cell subject is present.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    return CellIdLess{}(*left.cell, *right.cell);
}

/** \brief Test whether two errors have the same machine-readable identity. */
[[nodiscard]] bool same_error(const PhaseSupportError& left, const PhaseSupportError& right) noexcept
{
    if (left.rank != right.rank || left.code != right.code || left.phase != right.phase) {
        return false;
    }
    // Rift-generated errors with equal codes and phases have the same subject shape.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    return !left.cell.has_value() || same_cell_id(*left.cell, *right.cell);
}

/** \brief Sort errors and retain one diagnostic per structured defect. */
void sort_and_deduplicate(PhaseSupportErrors& errors)
{
    std::ranges::sort(errors, error_less);
    const auto duplicates = std::ranges::unique(errors, same_error);
    errors.erase(duplicates.begin(), duplicates.end());
}

/** \brief Serializable representation of one optional-subject public error. */
struct PhaseSupportErrorWire {
    std::uint8_t code = 0;
    unsigned int rank = 0;
    bool has_phase = false;
    std::uint32_t phase = 0;
    bool has_cell = false;
    dealii::CellId cell;
    std::string message;

    /** \brief Serialize the diagnostic for deal.II's generic all-gather. */
    template<class Archive> void serialize(Archive& archive, const unsigned int /*version*/)
    {
        archive & code;
        archive & rank;
        archive & has_phase;
        archive & phase;
        archive & has_cell;
        archive & cell;
        archive & message;
    }
};

/** \brief Failure-only payload containing rank metadata and full diagnostics. */
struct PhaseSupportFailurePacket {
    PhaseSupportValidationRecord record{};
    std::vector<PhaseSupportErrorWire> errors;

    /** \brief Serialize all failure detail for deal.II's generic all-gather. */
    template<class Archive> void serialize(Archive& archive, const unsigned int /*version*/)
    {
        archive & record;
        archive & errors;
    }
};

/** \brief Convert one public diagnostic to its private wire representation. */
[[nodiscard]] PhaseSupportErrorWire to_wire(const PhaseSupportError& error)
{
    return {.code = static_cast<std::uint8_t>(error.code),
            .rank = error.rank,
            .has_phase = error.phase.has_value(),
            .phase = error.phase.has_value() ? error.phase->value() : std::uint32_t{0},
            .has_cell = error.cell.has_value(),
            .cell = error.cell.value_or(dealii::CellId{}),
            .message = error.message};
}

/** \brief Restore one public diagnostic from its private wire representation. */
[[nodiscard]] PhaseSupportError from_wire(PhaseSupportErrorWire error)
{
    return {.code = static_cast<PhaseSupportErrorCode>(error.code),
            .rank = error.rank,
            .phase = error.has_phase ? std::optional{PhaseId::from_index(error.phase)} : std::nullopt,
            .cell = error.has_cell ? std::optional{error.cell} : std::nullopt,
            .message = std::move(error.message)};
}

/** \brief Canonically sort specifications and their owner-local cell requests. */
void sort_phase_support_specifications(std::vector<PhaseSupportSpecification>& specifications)
{
    std::ranges::sort(specifications, {},
                      [](const PhaseSupportSpecification& specification) { return specification.phase.value(); });
    for (auto& specification : specifications) {
        std::ranges::sort(specification.requested_cells, CellIdLess{});
    }
}

/** \brief Validate one rank's phase specification cardinality. */
void validate_phase_specifications(const PhaseGraph* phase_graph,
                                   const std::vector<PhaseSupportSpecification>& specifications,
                                   const unsigned int rank, PhaseSupportErrors& errors)
{
    if (phase_graph == nullptr) {
        add_error(errors, PhaseSupportErrorCode::phase_graph_unavailable, rank, std::nullopt, std::nullopt,
                  std::format("rank {} has no successfully created phase graph", rank));
        return;
    }

    const auto phase_count = phase_graph->phases().size();
    std::vector<std::size_t> specification_counts(phase_count, 0);
    for (const auto& specification : specifications) {
        const auto phase_index = static_cast<std::size_t>(specification.phase.value());
        if (phase_index < phase_count) {
            ++specification_counts.at(phase_index);
        }
        else {
            add_error(errors, PhaseSupportErrorCode::unknown_phase, rank, specification.phase, std::nullopt,
                      std::format("rank {} supplied a specification for unknown phase ID {}", rank,
                                  specification.phase.value()));
        }
    }

    for (std::size_t phase_index = 0; phase_index < phase_count; ++phase_index) {
        const auto phase = PhaseId::from_index(static_cast<PhaseId::representation_type>(phase_index));
        if (specification_counts.at(phase_index) == 0) {
            add_error(errors, PhaseSupportErrorCode::missing_phase_specification, rank, phase, std::nullopt,
                      std::format("rank {} supplied no specification for phase {} ({})", rank, phase.value(),
                                  phase_graph->phase(phase).name));
        }
        else if (specification_counts.at(phase_index) > 1) {
            add_error(errors, PhaseSupportErrorCode::duplicate_phase_specification, rank, phase, std::nullopt,
                      std::format("rank {} supplied {} specifications for phase {} ({})", rank,
                                  specification_counts.at(phase_index), phase.value(), phase_graph->phase(phase).name));
        }
    }
}

/** \brief Validate duplicate, presence, activity, and ownership cell rules. */
template<int dim>
void validate_requested_cells(const std::shared_ptr<const MeshSnapshot<dim>>& mesh,
                              const std::vector<PhaseSupportSpecification>& specifications, const unsigned int rank,
                              PhaseSupportErrors& errors)
{
    if (mesh == nullptr) {
        add_error(errors, PhaseSupportErrorCode::null_mesh, rank, std::nullopt, std::nullopt,
                  std::format("rank {} supplied no mesh snapshot", rank));
    }

    for (const auto& specification : specifications) {
        const auto& cells = specification.requested_cells;
        for (std::size_t index = 0; index < cells.size(); ++index) {
            const auto& cell_id = cells.at(index);
            if (index > 0 && same_cell_id(cells.at(index - 1), cell_id)) {
                add_error(errors, PhaseSupportErrorCode::duplicate_requested_cell, rank, specification.phase, cell_id,
                          std::format("rank {} phase {} repeats requested cell {}", rank, specification.phase.value(),
                                      format_cell_id<dim>(cell_id)));
            }

            if (mesh == nullptr) {
                continue;
            }
            const auto& triangulation = mesh->triangulation();
            if (!is_well_formed<dim>(cell_id) ||
                cell_id.get_coarse_cell_id() >= triangulation.n_global_coarse_cells() ||
                !triangulation.contains_cell(cell_id)) {
                add_error(errors, PhaseSupportErrorCode::cell_not_locally_present, rank, specification.phase, cell_id,
                          std::format("rank {} phase {} requested cell {}, which is not locally present", rank,
                                      specification.phase.value(), format_cell_id<dim>(cell_id)));
                continue;
            }

            const auto cell = triangulation.create_cell_iterator(cell_id);
            if (!cell->is_active()) {
                add_error(errors, PhaseSupportErrorCode::cell_not_active, rank, specification.phase, cell_id,
                          std::format("rank {} phase {} requested inactive cell {}", rank, specification.phase.value(),
                                      format_cell_id<dim>(cell_id)));
            }
            else if (!cell->is_locally_owned()) {
                add_error(errors, PhaseSupportErrorCode::cell_not_locally_owned, rank, specification.phase, cell_id,
                          std::format("rank {} phase {} requested cell {}, which is not locally owned", rank,
                                      specification.phase.value(), format_cell_id<dim>(cell_id)));
            }
        }
    }
}

/** \brief Build the constant-size record exchanged on every validation call. */
template<int dim>
[[nodiscard]] PhaseSupportValidationRecord make_validation_record(const PhaseGraph* phase_graph,
                                                                  const std::shared_ptr<const MeshSnapshot<dim>>& mesh,
                                                                  const bool local_inputs_valid)
{
    return PhaseSupportValidationRecord{{
        static_cast<std::uint64_t>(dim),
        static_cast<std::uint64_t>(phase_graph != nullptr),
        static_cast<std::uint64_t>(mesh != nullptr),
        mesh != nullptr ? mesh->id().value() : std::uint64_t{0},
        static_cast<std::uint64_t>(local_inputs_valid),
    }};
}

/** \brief Hold the two collective extrema of all fixed validation records. */
struct PhaseSupportValidationExtrema {
    PhaseSupportValidationRecord minimum;
    PhaseSupportValidationRecord maximum;
};

/** \brief Exchange two constant-size reductions for collective validation. */
[[nodiscard]] PhaseSupportValidationExtrema reduce_validation_record(const MPI_Comm communicator,
                                                                     const PhaseSupportValidationRecord& local_record)
{
    PhaseSupportValidationExtrema extrema{.minimum = local_record, .maximum = local_record};
    dealii::Utilities::MPI::min(local_record, communicator, extrema.minimum);
    dealii::Utilities::MPI::max(local_record, communicator, extrema.maximum);
    return extrema;
}

/** \brief Decide whether the fixed summaries require failure diagnostics. */
[[nodiscard]] bool validation_failed(const PhaseSupportValidationExtrema& extrema) noexcept
{
    return extrema.minimum != extrema.maximum ||
           extrema.minimum.at(field_index(PhaseSupportValidationField::local_inputs_valid)) == 0;
}

/** \brief Gather full diagnostics only after a fixed reduction reports failure. */
[[nodiscard]] PhaseSupportErrors collect_failure_errors(const MPI_Comm communicator,
                                                        const PhaseSupportValidationRecord& local_record,
                                                        const PhaseSupportErrors& local_errors)
{
    PhaseSupportFailurePacket local_packet{.record = local_record, .errors = {}};
    local_packet.errors.reserve(local_errors.size());
    std::ranges::transform(local_errors, std::back_inserter(local_packet.errors), to_wire);
    const auto packets = dealii::Utilities::MPI::all_gather(communicator, local_packet);

    PhaseSupportErrors errors;
    for (const auto& packet : packets) {
        for (const auto& error : packet.errors) {
            errors.push_back(from_wire(error));
        }
    }

    const auto rank_zero_dimension = packets.front().record.at(field_index(PhaseSupportValidationField::dimension));
    for (std::size_t rank_index = 1; rank_index < packets.size(); ++rank_index) {
        const auto dimension = packets.at(rank_index).record.at(field_index(PhaseSupportValidationField::dimension));
        if (dimension != rank_zero_dimension) {
            const auto rank = static_cast<unsigned int>(rank_index);
            add_error(errors, PhaseSupportErrorCode::dimension_mismatch, rank, std::nullopt, std::nullopt,
                      std::format("rank {} requested phase-support dimension {}, but rank 0 requested {}", rank,
                                  dimension, rank_zero_dimension));
        }
    }

    const auto reference = std::ranges::find_if(packets, [](const PhaseSupportFailurePacket& packet) {
        return packet.record.at(field_index(PhaseSupportValidationField::has_mesh)) != 0;
    });
    if (reference != packets.end()) {
        const auto reference_rank = static_cast<unsigned int>(std::distance(packets.begin(), reference));
        const auto reference_id = reference->record.at(field_index(PhaseSupportValidationField::mesh_snapshot_id));
        for (std::size_t rank_index = 0; rank_index < packets.size(); ++rank_index) {
            const auto& record = packets.at(rank_index).record;
            const auto mesh_is_present = record.at(field_index(PhaseSupportValidationField::has_mesh)) != 0;
            const auto mesh_id = record.at(field_index(PhaseSupportValidationField::mesh_snapshot_id));
            if (mesh_is_present && mesh_id != reference_id) {
                const auto rank = static_cast<unsigned int>(rank_index);
                add_error(errors, PhaseSupportErrorCode::mesh_snapshot_mismatch, rank, std::nullopt, std::nullopt,
                          std::format("rank {} supplied mesh snapshot {}, but rank {} supplied {}", rank, mesh_id,
                                      reference_rank, reference_id));
            }
        }
    }

    sort_and_deduplicate(errors);
    return errors;
}

/** \brief Canonical rank-local inputs retained after collective validation. */
template<int dim> struct ValidatedPhaseSupportInputs {
    std::shared_ptr<const MeshSnapshot<dim>> mesh;
    std::vector<PhaseSupportSpecification> specifications;
};

/** \brief Result used internally between validation and distributed closure. */
template<int dim>
using PhaseSupportValidationResult = std::expected<ValidatedPhaseSupportInputs<dim>, PhaseSupportErrors>;

/** \brief Validate and canonicalize all factory inputs with a bounded success path. */
template<int dim>
[[maybe_unused]] PhaseSupportValidationResult<dim>
validate_phase_support_inputs(const MPI_Comm communicator, const PhaseGraph* phase_graph, const unsigned int rank,
                              std::shared_ptr<const MeshSnapshot<dim>> mesh,
                              std::vector<PhaseSupportSpecification> specifications)
{
    sort_phase_support_specifications(specifications);

    PhaseSupportErrors local_errors;
    validate_phase_specifications(phase_graph, specifications, rank, local_errors);
    validate_requested_cells(mesh, specifications, rank, local_errors);
    sort_and_deduplicate(local_errors);

    const auto local_record = make_validation_record<dim>(phase_graph, mesh, local_errors.empty());
    const auto extrema = reduce_validation_record(communicator, local_record);
    if (validation_failed(extrema)) {
        return std::unexpected(collect_failure_errors(communicator, local_record, local_errors));
    }
    return ValidatedPhaseSupportInputs<dim>{.mesh = std::move(mesh), .specifications = std::move(specifications)};
}

/** \brief Describe one active owner or ghost cell in deterministic local order. */
struct LocalClosureCell {
    dealii::CellId id;
    unsigned int owner_rank;
    bool locally_owned;
    bool participates_in_closure;
};

/** \brief Store mesh-local fine-side groups as indices into one cell table. */
struct LocalClosureTopology {
    std::vector<LocalClosureCell> cells;
    std::vector<std::vector<std::size_t>> fine_side_groups;
};

/** \brief Find one known cell in the deterministic local closure table. */
[[nodiscard]] std::size_t find_cell_index(const LocalClosureTopology& topology, const dealii::CellId& id)
{
    const auto position =
        std::ranges::lower_bound(topology.cells, id, CellIdLess{},
                                 [](const LocalClosureCell& cell) -> const dealii::CellId& { return cell.id; });
    return static_cast<std::size_t>(std::distance(topology.cells.begin(), position));
}

/** \brief Build locally visible nonperiodic hanging-face closure relations. */
template<int dim> [[nodiscard]] LocalClosureTopology build_local_closure_topology(const MeshSnapshot<dim>& mesh)
{
    LocalClosureTopology topology;
    const auto& triangulation = mesh.triangulation();
    for (const auto& cell : triangulation.active_cell_iterators()) {
        if (!cell->is_artificial()) {
            topology.cells.push_back({.id = cell->id(),
                                      .owner_rank = static_cast<unsigned int>(cell->subdomain_id()),
                                      .locally_owned = cell->is_locally_owned(),
                                      .participates_in_closure = false});
        }
    }
    std::ranges::sort(topology.cells, [](const LocalClosureCell& left, const LocalClosureCell& right) {
        return CellIdLess{}(left.id, right.id);
    });

    for (const auto& coarse_cell : triangulation.active_cell_iterators()) {
        if (coarse_cell->is_artificial()) {
            continue;
        }
        for (const auto face : coarse_cell->face_indices()) {
            if (coarse_cell->at_boundary(face) || !coarse_cell->face(face)->has_children()) {
                continue;
            }

            std::vector<std::size_t> group;
            const auto subface_count = coarse_cell->face(face)->n_active_descendants();
            group.reserve(subface_count);
            for (unsigned int subface = 0; subface < subface_count; ++subface) {
                const auto fine_cell = coarse_cell->neighbor_child_on_subface(face, subface);
                if (!fine_cell->is_artificial()) {
                    group.push_back(find_cell_index(topology, fine_cell->id()));
                }
            }
            std::ranges::sort(group);
            group.erase(std::ranges::unique(group).begin(), group.end());
            // A locally trivial group is a semantic no-op during saturation.
            topology.fine_side_groups.push_back(std::move(group));
        }
    }

    std::ranges::sort(topology.fine_side_groups);
    topology.fine_side_groups.erase(std::ranges::unique(topology.fine_side_groups).begin(),
                                    topology.fine_side_groups.end());
    for (const auto& group : topology.fine_side_groups) {
        for (const auto cell : group) {
            topology.cells.at(cell).participates_in_closure = true;
        }
    }
    return topology;
}

/** \brief Hold packed phase flags on the locally relevant active-cell table. */
struct LocalPhaseSupportState {
    LocalClosureTopology topology;
    std::size_t blocks_per_cell;
    std::vector<std::uint64_t> phase_flags;
    std::vector<unsigned int> ghost_owner_ranks;
};

/** \brief Number of phase flags stored in one fixed-width communication block. */
constexpr std::size_t phase_flags_per_block = std::numeric_limits<std::uint64_t>::digits;

/** \brief Find owners of ghost cells that participate in local closure. */
[[nodiscard]] std::vector<unsigned int> find_relevant_ghost_owners(const LocalClosureTopology& topology)
{
    std::vector<unsigned int> owners;
    for (const auto& cell : topology.cells) {
        if (!cell.locally_owned && cell.participates_in_closure) {
            owners.push_back(cell.owner_rank);
        }
    }
    std::ranges::sort(owners);
    owners.erase(std::ranges::unique(owners).begin(), owners.end());
    return owners;
}

/** \brief Activate every validated owner-local requested phase flag. */
void activate_requested_cells(LocalPhaseSupportState& state,
                              const std::vector<PhaseSupportSpecification>& specifications) noexcept
{
    for (const auto& specification : specifications) {
        const auto phase = static_cast<std::size_t>(specification.phase.value());
        const auto block = phase / phase_flags_per_block;
        const auto mask = std::uint64_t{1} << (phase % phase_flags_per_block);
        for (const auto& cell : specification.requested_cells) {
            const auto cell_index = find_cell_index(state.topology, cell);
            state.phase_flags.at((cell_index * state.blocks_per_cell) + block) |= mask;
        }
    }
}

/** \brief Create packed local state from validated canonical specifications. */
template<int dim>
[[nodiscard]] LocalPhaseSupportState
make_local_phase_support_state(const MeshSnapshot<dim>& mesh,
                               const std::vector<PhaseSupportSpecification>& specifications,
                               const std::size_t phase_count)
{
    auto topology = build_local_closure_topology(mesh);
    const auto blocks_per_cell = (phase_count + phase_flags_per_block - 1) / phase_flags_per_block;
    auto ghost_owner_ranks = find_relevant_ghost_owners(topology);
    LocalPhaseSupportState state{.topology = std::move(topology),
                                 .blocks_per_cell = blocks_per_cell,
                                 .phase_flags = {},
                                 .ghost_owner_ranks = std::move(ghost_owner_ranks)};
    state.phase_flags.resize(state.topology.cells.size() * blocks_per_cell, 0);
    activate_requested_cells(state, specifications);
    return state;
}

/** \brief Saturate all phases over the locally visible fine-side groups. */
[[nodiscard]] bool saturate_local_phase_support(LocalPhaseSupportState& state) noexcept
{
    auto changed_any = false;
    auto sweep_required = true;
    while (sweep_required) {
        sweep_required = false;
        for (const auto& group : state.topology.fine_side_groups) {
            for (std::size_t block = 0; block < state.blocks_per_cell; ++block) {
                std::uint64_t group_flags = 0;
                for (const auto cell : group) {
                    group_flags |= state.phase_flags.at((cell * state.blocks_per_cell) + block);
                }
                for (const auto cell : group) {
                    auto& cell_flags = state.phase_flags.at((cell * state.blocks_per_cell) + block);
                    const auto added_flags = group_flags & ~cell_flags;
                    cell_flags |= group_flags;
                    sweep_required = sweep_required || added_flags != 0;
                }
            }
        }
        changed_any = changed_any || sweep_required;
    }
    return changed_any;
}

/** \brief Copy one cell's complete packed phase-support payload. */
[[nodiscard]] std::vector<std::uint64_t> copy_phase_flags(const LocalPhaseSupportState& state, const std::size_t cell)
{
    const auto first = state.phase_flags.begin() + static_cast<std::ptrdiff_t>(cell * state.blocks_per_cell);
    return {first, first + static_cast<std::ptrdiff_t>(state.blocks_per_cell)};
}

/**
 * \brief Publish relevant owner flags to ghost copies in one batched exchange.
 *
 * deal.II partitions p4est for one-level coarsening, so the fine-side cells
 * of each closure group share one owner. That owner can compute the complete
 * group locally; ghosts consume its authoritative flags but never originate
 * activations that need to be returned.
 */
template<int dim>
[[nodiscard]] bool publish_owner_phase_flags_to_ghosts(const MeshSnapshot<dim>& mesh, LocalPhaseSupportState& state)
{
    using PackedPhaseFlags = std::vector<std::uint64_t>;
    using Triangulation = dealii::parallel::distributed::Triangulation<dim>;
    using ActiveCellIterator = Triangulation::active_cell_iterator;

    const auto pack = [&state](const ActiveCellIterator& cell) -> PackedPhaseFlags {
        return copy_phase_flags(state, find_cell_index(state.topology, cell->id()));
    };

    bool changed = false;
    const auto unpack = [&state, &changed](const ActiveCellIterator& cell, const PackedPhaseFlags& owner_flags) {
        const auto cell_index = find_cell_index(state.topology, cell->id());
        for (std::size_t block = 0; block < state.blocks_per_cell; ++block) {
            auto& ghost_flags = state.phase_flags.at((cell_index * state.blocks_per_cell) + block);
            const auto owner_block_flags = owner_flags.at(block);
            const auto added_flags = owner_block_flags & ~ghost_flags;
            ghost_flags |= owner_block_flags;
            changed = changed || added_flags != 0;
        }
    };

    const auto request_relevant_ghost = [&state](const ActiveCellIterator& cell) {
        return state.topology.cells.at(find_cell_index(state.topology, cell->id())).participates_in_closure;
    };

    dealii::GridTools::exchange_cell_data_to_ghosts<PackedPhaseFlags>(mesh.triangulation(), pack, unpack,
                                                                      request_relevant_ghost);
    return changed;
}

/** \brief Report whether any rank changed support during the current round. */
[[nodiscard]] bool any_rank_changed(const MPI_Comm communicator, const bool local_changed)
{
    return dealii::Utilities::MPI::max(static_cast<unsigned int>(local_changed), communicator) != 0;
}

/** \brief Test one phase bit on one locally indexed active cell. */
[[nodiscard]] bool phase_is_supported(const LocalPhaseSupportState& state, const PhaseId phase,
                                      const std::size_t cell) noexcept
{
    const auto phase_index = static_cast<std::size_t>(phase.value());
    const auto block = phase_index / phase_flags_per_block;
    const auto mask = std::uint64_t{1} << (phase_index % phase_flags_per_block);
    return (state.phase_flags.at((cell * state.blocks_per_cell) + block) & mask) != 0;
}

/** \brief Compute the complete distributed least fixed point for all phases. */
template<int dim>
[[nodiscard]] LocalPhaseSupportState
close_distributed_phase_support(const MeshSnapshot<dim>& mesh,
                                const std::vector<PhaseSupportSpecification>& specifications,
                                const std::size_t phase_count)
{
    auto state = make_local_phase_support_state(mesh, specifications, phase_count);
    auto globally_changed = true;
    while (globally_changed) {
        const auto local_closure_changed = saturate_local_phase_support(state);
        const auto ghost_publication_changed = publish_owner_phase_flags_to_ghosts(mesh, state);
        const auto locally_changed = local_closure_changed || ghost_publication_changed;
        globally_changed = any_rank_changed(mesh.communicator(), locally_changed);
    }
    return state;
}

} // namespace

PhaseSupport::PhaseSupport(const PhaseId phase, std::vector<dealii::CellId> cells,
                           const std::size_t requested_count) noexcept :
    phase_(phase), cells_(std::move(cells)), requested_count_(requested_count)
{
}

std::span<const dealii::CellId> PhaseSupport::requested_cells() const noexcept
{
    return std::span<const dealii::CellId>{cells_}.first(requested_count_);
}

std::span<const dealii::CellId> PhaseSupport::closure_added_cells() const noexcept
{
    return std::span<const dealii::CellId>{cells_}.subspan(requested_count_);
}

std::span<const dealii::CellId> PhaseSupport::closed_cells() const noexcept { return cells_; }

template<int dim>
    requires(dim == 2 || dim == 3)
PhaseSupportSet<dim>::PhaseSupportSet(std::shared_ptr<const MeshSnapshot<dim>> mesh,
                                      std::vector<PhaseSupport> supports) noexcept :
    mesh_(std::move(mesh)), supports_(std::move(supports))
{
}

template<int dim>
    requires(dim == 2 || dim == 3)
const MeshSnapshot<dim>& PhaseSupportSet<dim>::mesh_snapshot() const noexcept
{
    return *mesh_;
}

template<int dim>
    requires(dim == 2 || dim == 3)
std::span<const PhaseSupport> PhaseSupportSet<dim>::supports() const noexcept
{
    return supports_;
}

template<int dim>
    requires(dim == 2 || dim == 3)
const PhaseSupport& PhaseSupportSet<dim>::support(const PhaseId phase) const
{
    return supports_.at(phase.value());
}

template<int dim>
    requires(dim == 2 || dim == 3)
PhaseSupportResult<dim> RiftContext::create_phase_supports(std::shared_ptr<const MeshSnapshot<dim>> mesh,
                                                           std::vector<PhaseSupportSpecification> specifications)
{
    auto validation = validate_phase_support_inputs<dim>(mpi_communicator(), phase_graph_.get(), this_mpi_process(),
                                                         std::move(mesh), std::move(specifications));
    if (!validation.has_value()) {
        return std::unexpected(std::move(validation.error()));
    }

    auto inputs = std::move(validation.value());
    auto state = close_distributed_phase_support(*inputs.mesh, inputs.specifications, phase_graph_->phases().size());

    std::vector<PhaseSupport> supports;
    supports.reserve(inputs.specifications.size());
    for (auto& specification : inputs.specifications) {
        auto cells = std::move(specification.requested_cells);
        const auto requested_count = cells.size();

        for (std::size_t cell = 0; cell < state.topology.cells.size(); ++cell) {
            const auto& closure_cell = state.topology.cells.at(cell);
            if (!closure_cell.locally_owned || !phase_is_supported(state, specification.phase, cell)) {
                continue;
            }

            const auto requested_cells = std::span<const dealii::CellId>{cells}.first(requested_count);
            if (!std::ranges::binary_search(requested_cells, closure_cell.id, CellIdLess{})) {
                cells.push_back(closure_cell.id);
            }
        }

        PhaseSupport support(specification.phase, std::move(cells), requested_count);
        supports.push_back(std::move(support));
    }

    return PhaseSupportSet<dim>(std::move(inputs.mesh), std::move(supports));
}

template class PhaseSupportSet<2>;
template class PhaseSupportSet<3>;

template PhaseSupportResult<2> RiftContext::create_phase_supports<2>(std::shared_ptr<const MeshSnapshot<2>>,
                                                                     std::vector<PhaseSupportSpecification>);
template PhaseSupportResult<3> RiftContext::create_phase_supports<3>(std::shared_ptr<const MeshSnapshot<3>>,
                                                                     std::vector<PhaseSupportSpecification>);

} // namespace rift
