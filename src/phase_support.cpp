/**
 * \file
 * \brief Immutable phase-support value and view implementation.
 */

#include <algorithm>
#include <array>
#include <boost/serialization/array.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include <cstddef>
#include <cstdint>
#include <deal.II/base/exceptions.h>
#include <deal.II/base/geometry_info.h>
#include <deal.II/base/mpi.h>
#include <deal.II/base/numbers.h>
#include <deal.II/grid/cell_id.h>
#include <expected>
#include <format>
#include <iterator>
#include <memory>
#include <mpi.h>
#include <optional>
#include <ranges>
#include <rift/phase_support.hpp>
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
    const CellIdLess less;
    return !less(left, right) && !less(right, left);
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
    return is_well_formed<dim>(cell) ? cell.to_string() : std::string{"<invalid CellId>"};
}

/** \brief Append one structured rank-local phase-support error. */
void add_error(PhaseSupportErrors& errors, const PhaseSupportErrorCode code, const unsigned int rank,
               std::optional<PhaseId> phase, std::optional<dealii::CellId> cell, std::string message)
{
    errors.push_back({.code = code,
                      .rank = rank,
                      .phase = std::move(phase),
                      .cell = std::move(cell),
                      .message = std::move(message)});
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
    return CellIdLess{}(*left.cell, *right.cell);
}

/** \brief Test whether two errors have the same machine-readable identity. */
[[nodiscard]] bool same_error(const PhaseSupportError& left, const PhaseSupportError& right) noexcept
{
    if (left.rank != right.rank || left.code != right.code || left.phase != right.phase ||
        left.cell.has_value() != right.cell.has_value()) {
        return false;
    }
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
            .cell = error.has_cell ? std::optional{std::move(error.cell)} : std::nullopt,
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
            ++specification_counts[phase_index];
        }
        else {
            add_error(errors, PhaseSupportErrorCode::unknown_phase, rank, specification.phase, std::nullopt,
                      std::format("rank {} supplied a specification for unknown phase ID {}", rank,
                                  specification.phase.value()));
        }
    }

    for (std::size_t phase_index = 0; phase_index < phase_count; ++phase_index) {
        const auto phase = PhaseId::from_index(static_cast<PhaseId::representation_type>(phase_index));
        if (specification_counts[phase_index] == 0) {
            add_error(errors, PhaseSupportErrorCode::missing_phase_specification, rank, phase, std::nullopt,
                      std::format("rank {} supplied no specification for phase {} ({})", rank, phase.value(),
                                  phase_graph->phase(phase).name));
        }
        else if (specification_counts[phase_index] > 1) {
            add_error(errors, PhaseSupportErrorCode::duplicate_phase_specification, rank, phase, std::nullopt,
                      std::format("rank {} supplied {} specifications for phase {} ({})", rank,
                                  specification_counts[phase_index], phase.value(), phase_graph->phase(phase).name));
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
            const auto& cell_id = cells[index];
            if (index > 0 && same_cell_id(cells[index - 1], cell_id)) {
                add_error(errors, PhaseSupportErrorCode::duplicate_requested_cell, rank, specification.phase, cell_id,
                          std::format("rank {} phase {} repeats requested cell {}", rank, specification.phase.value(),
                                      format_cell_id<dim>(cell_id)));
            }

            if (mesh == nullptr) {
                continue;
            }
            const auto& triangulation = mesh->triangulation();
            if (!is_well_formed<dim>(cell_id) || !triangulation.contains_cell(cell_id)) {
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
    auto status = MPI_Allreduce(local_record.data(), extrema.minimum.data(), static_cast<int>(local_record.size()),
                                dealii::Utilities::MPI::mpi_type_id_for_type<std::uint64_t>, MPI_MIN, communicator);
    if (status != MPI_SUCCESS) {      // GCOVR_EXCL_BR_LINE
        throw dealii::ExcMPI(status); // GCOVR_EXCL_LINE
    }
    status = MPI_Allreduce(local_record.data(), extrema.maximum.data(), static_cast<int>(local_record.size()),
                           dealii::Utilities::MPI::mpi_type_id_for_type<std::uint64_t>, MPI_MAX, communicator);
    if (status != MPI_SUCCESS) {      // GCOVR_EXCL_BR_LINE
        throw dealii::ExcMPI(status); // GCOVR_EXCL_LINE
    }
    return extrema;
}

/** \brief Decide whether the fixed summaries require failure diagnostics. */
[[nodiscard]] bool validation_failed(const PhaseSupportValidationExtrema& extrema) noexcept
{
    return extrema.minimum != extrema.maximum ||
           extrema.minimum[field_index(PhaseSupportValidationField::local_inputs_valid)] == 0;
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

    const auto rank_zero_dimension = packets.front().record[field_index(PhaseSupportValidationField::dimension)];
    for (std::size_t rank_index = 1; rank_index < packets.size(); ++rank_index) {
        const auto dimension = packets[rank_index].record[field_index(PhaseSupportValidationField::dimension)];
        if (dimension != rank_zero_dimension) {
            const auto rank = static_cast<unsigned int>(rank_index);
            add_error(errors, PhaseSupportErrorCode::dimension_mismatch, rank, std::nullopt, std::nullopt,
                      std::format("rank {} requested phase-support dimension {}, but rank 0 requested {}", rank,
                                  dimension, rank_zero_dimension));
        }
    }

    const auto reference = std::ranges::find_if(packets, [](const PhaseSupportFailurePacket& packet) {
        return packet.record[field_index(PhaseSupportValidationField::has_mesh)] != 0;
    });
    if (reference != packets.end()) {
        const auto reference_rank = static_cast<unsigned int>(std::distance(packets.begin(), reference));
        const auto reference_id = reference->record[field_index(PhaseSupportValidationField::mesh_snapshot_id)];
        for (std::size_t rank_index = 0; rank_index < packets.size(); ++rank_index) {
            const auto& record = packets[rank_index].record;
            const auto mesh_is_present = record[field_index(PhaseSupportValidationField::has_mesh)] != 0;
            const auto mesh_id = record[field_index(PhaseSupportValidationField::mesh_snapshot_id)];
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
validate_phase_support_inputs(const MPI_Comm communicator, const unsigned int rank, const PhaseGraph* phase_graph,
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

/** \brief Compile both supported validation paths before factory integration. */
[[maybe_unused]] constexpr auto validate_phase_support_inputs_2d = &validate_phase_support_inputs<2>;
/** \brief Compile both supported validation paths before factory integration. */
[[maybe_unused]] constexpr auto validate_phase_support_inputs_3d = &validate_phase_support_inputs<3>;

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

template class PhaseSupportSet<2>;
template class PhaseSupportSet<3>;

} // namespace rift
