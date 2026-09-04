#include <algorithm>
#include <boost/serialization/string.hpp> // IWYU pragma: keep
#include <boost/serialization/vector.hpp> // IWYU pragma: keep
#include <boost/ut.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deal.II/base/mpi.h>
#include <deal.II/base/point.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/fe/mapping_q1.h>
#include <deal.II/grid/cell_id.h>
#include <deal.II/grid/grid_generator.h>
#include <format>
#include <map>
#include <memory>
#include <mpi.h>
#include <optional>
#include <ranges>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/phase_support.hpp>
#include <rift/rift_context.hpp>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t phase_count = 66;

/** One owner-local cell record gathered by the independent test oracle. */
struct OwnedCellWire {
    std::string id;
    unsigned int owner = 0;

    template<class Archive> void serialize(Archive& archive, const unsigned int /*version*/)
    {
        archive & id;
        archive & owner;
    }
};

/** One ghost cell and the rank on which that ghost copy is present. */
struct GhostCellWire {
    std::string id;
    unsigned int owner = 0;
    unsigned int requester = 0;

    template<class Archive> void serialize(Archive& archive, const unsigned int /*version*/)
    {
        archive & id;
        archive & owner;
        archive & requester;
    }
};

/** Globally reconstructed hanging-face hypergraph and cell ownership. */
struct OracleTopology {
    std::vector<std::vector<std::string>> groups;
    std::map<std::string, unsigned int> owners;
    std::vector<GhostCellWire> ghosts;
};

[[nodiscard]] rift::PhaseGraphSpecification phase_graph_specification()
{
    rift::PhaseGraphSpecification specification;
    specification.phases.reserve(phase_count);
    for (std::size_t phase = 0; phase < phase_count; ++phase) {
        specification.phases.push_back(
            {.name = std::format("phase-{:02}", phase), .physics_key = rift::PhysicsKey{"test-physics"}});
    }
    return specification;
}

[[nodiscard]] std::vector<rift::PhaseSupportSpecification>
empty_support_specifications(const std::optional<std::pair<rift::PhaseId, dealii::CellId>>& request = std::nullopt)
{
    std::vector<rift::PhaseSupportSpecification> specifications;
    specifications.reserve(phase_count);
    for (std::size_t phase = 0; phase < phase_count; ++phase) {
        auto cells = std::vector<dealii::CellId>{};
        if (request.has_value() && request->first.value() == phase) {
            cells.push_back(request->second);
        }
        specifications.push_back({.phase = rift::PhaseId::from_index(static_cast<std::uint32_t>(phase)),
                                  .requested_cells = std::move(cells)});
    }
    return specifications;
}

[[nodiscard]] std::vector<rift::PhaseSupportSpecification>
two_block_support_specifications(const unsigned int rank, const unsigned int owner, const dealii::CellId& requested)
{
    auto specifications = empty_support_specifications();
    if (rank == owner) {
        specifications.front().requested_cells.push_back(requested);
        specifications.back().requested_cells.push_back(requested);
    }
    std::ranges::reverse(specifications);
    return specifications;
}

template<int dim>
[[nodiscard]] std::shared_ptr<const rift::MeshSnapshot<dim>>
publish_snapshot(rift::RiftContext& context,
                 std::unique_ptr<dealii::parallel::distributed::Triangulation<dim>> triangulation)
{
    auto result =
        context.create_mesh_snapshot<dim>(std::move(triangulation), std::make_unique<dealii::MappingQ1<dim>>());
    boost::ut::expect(result.has_value());
    return result.has_value() ? *result : nullptr;
}

template<int dim>
[[nodiscard]] std::shared_ptr<const rift::MeshSnapshot<dim>> make_adaptive_snapshot(rift::RiftContext& context)
{
    auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_WORLD);
    const std::vector<unsigned int> subdivisions(dim, 3U);
    dealii::Point<dim> upper_corner;
    for (unsigned int direction = 0; std::cmp_less(direction, dim); ++direction) {
        // The loop proves that the Point index is in range.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        upper_corner[direction] = 3.0;
    }
    dealii::GridGenerator::subdivided_hyper_rectangle(*triangulation, subdivisions, dealii::Point<dim>{}, upper_corner);
    dealii::Point<dim> center;
    for (unsigned int direction = 0; std::cmp_less(direction, dim); ++direction) {
        // The loop proves that the Point index is in range.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        center[direction] = 1.5;
    }
    for (const auto& cell : triangulation->active_cell_iterators()) {
        if (cell->is_locally_owned() && cell->center().distance(center) < 0.1) {
            cell->set_refine_flag();
        }
    }
    triangulation->execute_coarsening_and_refinement();
    return publish_snapshot(context, std::move(triangulation));
}

template<int dim>
[[nodiscard]] std::shared_ptr<const rift::MeshSnapshot<dim>> make_uniform_snapshot(rift::RiftContext& context)
{
    auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_WORLD);
    dealii::GridGenerator::subdivided_hyper_cube(*triangulation, 4);
    return publish_snapshot(context, std::move(triangulation));
}

// This independent mesh oracle intentionally keeps the full traversal visible.
// NOLINTBEGIN(readability-function-cognitive-complexity)
template<int dim>
[[nodiscard]] OracleTopology make_oracle_topology(const rift::MeshSnapshot<dim>& snapshot, const unsigned int rank)
{
    std::vector<std::vector<std::string>> local_groups;
    std::vector<OwnedCellWire> local_owned_cells;
    std::vector<GhostCellWire> local_ghosts;

    for (const auto& cell : snapshot.triangulation().active_cell_iterators()) {
        if (cell->is_locally_owned()) {
            local_owned_cells.push_back({.id = cell->id().to_string(), .owner = rank});
        }
        else if (cell->is_ghost()) {
            local_ghosts.push_back({.id = cell->id().to_string(),
                                    .owner = static_cast<unsigned int>(cell->subdomain_id()),
                                    .requester = rank});
        }

        if (cell->is_artificial()) {
            continue;
        }
        for (const auto face : cell->face_indices()) {
            if (cell->at_boundary(face) || !cell->face(face)->has_children()) {
                continue;
            }

            std::vector<std::string> group;
            const auto subface_count = cell->face(face)->n_active_descendants();
            for (unsigned int subface = 0; subface < subface_count; ++subface) {
                const auto fine_cell = cell->neighbor_child_on_subface(face, subface);
                if (!fine_cell->is_artificial()) {
                    group.push_back(fine_cell->id().to_string());
                }
            }
            std::ranges::sort(group);
            group.erase(std::ranges::unique(group).begin(), group.end());
            if (group.size() > 1) {
                local_groups.push_back(std::move(group));
            }
        }
    }

    const auto communicator = snapshot.communicator();
    const auto gathered_groups = dealii::Utilities::MPI::all_gather(communicator, local_groups);
    const auto gathered_owned_cells = dealii::Utilities::MPI::all_gather(communicator, local_owned_cells);
    const auto gathered_ghosts = dealii::Utilities::MPI::all_gather(communicator, local_ghosts);

    OracleTopology topology;
    for (const auto& rank_groups : gathered_groups) {
        topology.groups.insert(topology.groups.end(), rank_groups.begin(), rank_groups.end());
    }
    std::ranges::sort(topology.groups);
    topology.groups.erase(std::ranges::unique(topology.groups).begin(), topology.groups.end());

    for (const auto& rank_cells : gathered_owned_cells) {
        for (const auto& cell : rank_cells) {
            topology.owners.emplace(cell.id, cell.owner);
        }
    }
    for (const auto& rank_ghosts : gathered_ghosts) {
        topology.ghosts.insert(topology.ghosts.end(), rank_ghosts.begin(), rank_ghosts.end());
    }
    std::ranges::sort(topology.ghosts, {}, &GhostCellWire::id);
    return topology;
}
// NOLINTEND(readability-function-cognitive-complexity)

[[nodiscard]] bool is_visible_across_partition(const std::vector<std::string>& group,
                                               const std::vector<GhostCellWire>& ghosts)
{
    return std::ranges::any_of(group, [&](const std::string& cell) {
        return std::ranges::any_of(ghosts, [&](const GhostCellWire& ghost) { return ghost.id == cell; });
    });
}

[[nodiscard]] std::set<std::string> independent_fixed_point(const std::vector<std::vector<std::string>>& groups,
                                                            const std::string& requested)
{
    std::set<std::string> closed{requested};
    while (true) {
        auto changed = false;
        for (const auto& group : groups) {
            const auto active =
                std::ranges::any_of(group, [&](const std::string& cell) { return closed.contains(cell); });
            if (!active) {
                continue;
            }
            for (const auto& cell : group) {
                changed = closed.insert(cell).second || changed;
            }
        }
        if (!changed) {
            break;
        }
    }
    return closed;
}

[[nodiscard]] bool cell_id_less(const dealii::CellId& left, const dealii::CellId& right) { return left < right; }

[[nodiscard]] std::vector<dealii::CellId> owner_local_cells(const std::set<std::string>& closed,
                                                            const std::map<std::string, unsigned int>& owners,
                                                            const unsigned int rank,
                                                            const std::optional<std::string>& excluded = std::nullopt)
{
    std::vector<dealii::CellId> cells;
    for (const auto& cell : closed) {
        if (owners.at(cell) == rank && (!excluded.has_value() || cell != *excluded)) {
            cells.emplace_back(cell);
        }
    }
    std::ranges::sort(cells, cell_id_less);
    return cells;
}

void expect_cells(const std::span<const dealii::CellId> actual, const std::vector<dealii::CellId>& expected)
{
    boost::ut::expect(std::ranges::equal(actual, expected));
}

void expect_distributed_support(const rift::PhaseSupport& support, const std::set<std::string>& fixed_point,
                                const std::map<std::string, unsigned int>& owners, const unsigned int rank,
                                const unsigned int requested_owner, const std::string& requested)
{
    const auto expected_requested =
        rank == requested_owner ? std::vector{dealii::CellId{requested}} : std::vector<dealii::CellId>{};
    const auto expected_added = owner_local_cells(fixed_point, owners, rank, requested);
    auto expected_closed = expected_requested;
    expected_closed.insert(expected_closed.end(), expected_added.begin(), expected_added.end());

    expect_cells(support.requested_cells(), expected_requested);
    expect_cells(support.closure_added_cells(), expected_added);
    expect_cells(support.closed_cells(), expected_closed);
}

template<int dim> void test_distributed_fixed_point(rift::RiftContext& context)
{
    const auto snapshot = make_adaptive_snapshot<dim>(context);
    if (snapshot == nullptr) {
        return;
    }
    const auto rank = context.this_mpi_process();
    const auto topology = make_oracle_topology(*snapshot, rank);
    const auto cross_partition_group = std::ranges::find_if(
        topology.groups, [&](const auto& group) { return is_visible_across_partition(group, topology.ghosts); });
    const auto selected_group = context.n_mpi_processes() == 1 ? topology.groups.begin() : cross_partition_group;
    boost::ut::expect(selected_group != topology.groups.end());
    if (selected_group == topology.groups.end()) {
        return;
    }

    const auto requested = selected_group->back();
    const auto requested_owner = topology.owners.at(requested);
    const auto fixed_point = independent_fixed_point(topology.groups, requested);
    const auto result = context.create_phase_supports<dim>(
        snapshot, two_block_support_specifications(rank, requested_owner, dealii::CellId{requested}));
    boost::ut::expect(result.has_value());
    if (!result.has_value()) {
        return;
    }

    boost::ut::expect(result->supports().size() == phase_count);
    expect_distributed_support(result->support(rift::PhaseId::from_index(0)), fixed_point, topology.owners, rank,
                               requested_owner, requested);
    expect_distributed_support(result->support(rift::PhaseId::from_index(65)), fixed_point, topology.owners, rank,
                               requested_owner, requested);
    boost::ut::expect(result->support(rift::PhaseId::from_index(1)).closed_cells().empty());
}

void test_collective_phase_errors(rift::RiftContext& context)
{
    const auto snapshot = make_uniform_snapshot<2>(context);
    if (snapshot == nullptr) {
        return;
    }
    auto specifications = empty_support_specifications();
    if (context.this_mpi_process() == 0) {
        specifications.pop_back();
    }

    const auto result = context.create_phase_supports<2>(snapshot, std::move(specifications));
    boost::ut::expect(not result.has_value());
    if (result.has_value()) {
        return;
    }
    boost::ut::expect(result.error().size() == std::size_t{1});
    if (result.error().size() == 1) {
        const auto& error = result.error().front();
        boost::ut::expect(error.code == rift::PhaseSupportErrorCode::missing_phase_specification);
        boost::ut::expect(error.rank == 0U);
        boost::ut::expect(error.phase == rift::PhaseId::from_index(65));
        boost::ut::expect(not error.cell.has_value());
    }
}

template<int dim> void test_nonowner_request(rift::RiftContext& context)
{
    const auto snapshot = make_adaptive_snapshot<dim>(context);
    if (snapshot == nullptr) {
        return;
    }
    if (context.n_mpi_processes() == 1) {
        const auto result = context.create_phase_supports<dim>(snapshot, empty_support_specifications());
        boost::ut::expect(result.has_value());
        return;
    }

    const auto topology = make_oracle_topology(*snapshot, context.this_mpi_process());
    boost::ut::expect(not topology.ghosts.empty());
    if (topology.ghosts.empty()) {
        return;
    }
    const auto ghost = topology.ghosts.front();
    auto specifications = empty_support_specifications();
    if (context.this_mpi_process() == ghost.requester) {
        specifications.front().requested_cells.emplace_back(ghost.id);
    }

    const auto result = context.create_phase_supports<dim>(snapshot, std::move(specifications));
    boost::ut::expect(not result.has_value());
    if (result.has_value()) {
        return;
    }
    boost::ut::expect(result.error().size() == std::size_t{1});
    if (result.error().size() == 1) {
        const auto& error = result.error().front();
        boost::ut::expect(error.code == rift::PhaseSupportErrorCode::cell_not_locally_owned);
        boost::ut::expect(error.rank == ghost.requester);
        boost::ut::expect(error.phase == rift::PhaseId::from_index(0));
        boost::ut::expect(error.cell == dealii::CellId{ghost.id});
    }
}

void test_null_mesh_agreement(rift::RiftContext& context)
{
    const auto snapshot = make_uniform_snapshot<2>(context);
    if (snapshot == nullptr) {
        return;
    }

    const auto mesh = context.this_mpi_process() == 0 ? std::shared_ptr<const rift::MeshSnapshot<2>>{} : snapshot;
    const auto result = context.create_phase_supports<2>(mesh, empty_support_specifications());
    boost::ut::expect(not result.has_value());
    if (result.has_value()) {
        return;
    }
    boost::ut::expect(result.error().size() == std::size_t{1});
    if (result.error().size() == 1) {
        boost::ut::expect(result.error().front().code == rift::PhaseSupportErrorCode::null_mesh);
        boost::ut::expect(result.error().front().rank == 0U);
    }
}

void test_mesh_snapshot_agreement(rift::RiftContext& context)
{
    const auto first = make_uniform_snapshot<2>(context);
    const auto second = make_uniform_snapshot<2>(context);
    if (first == nullptr || second == nullptr) {
        return;
    }

    const auto rank = context.this_mpi_process();
    const auto result = context.create_phase_supports<2>(rank == 0 ? first : second, empty_support_specifications());
    if (context.n_mpi_processes() == 1) {
        boost::ut::expect(result.has_value());
        return;
    }

    boost::ut::expect(not result.has_value());
    if (result.has_value()) {
        return;
    }
    boost::ut::expect(result.error().size() == context.n_mpi_processes() - 1U);
    for (std::size_t index = 0; index < result.error().size(); ++index) {
        const auto& error = result.error().at(index);
        boost::ut::expect(error.code == rift::PhaseSupportErrorCode::mesh_snapshot_mismatch);
        boost::ut::expect(error.rank == index + 1U);
        boost::ut::expect(not error.phase.has_value());
        boost::ut::expect(not error.cell.has_value());
    }
}

void test_dimension_agreement(rift::RiftContext& context)
{
    const auto snapshot_2d = make_uniform_snapshot<2>(context);
    const auto snapshot_3d = make_uniform_snapshot<3>(context);
    if (snapshot_2d == nullptr || snapshot_3d == nullptr) {
        return;
    }

    const auto rank = context.this_mpi_process();
    if (context.n_mpi_processes() == 1) {
        const auto result = context.create_phase_supports<3>(snapshot_3d, empty_support_specifications());
        boost::ut::expect(result.has_value());
        return;
    }

    const auto expected_error_count = 2U * (context.n_mpi_processes() - 1U);
    if (rank == 0) {
        const auto result = context.create_phase_supports<2>(snapshot_2d, empty_support_specifications());
        boost::ut::expect(not result.has_value());
        if (!result.has_value()) {
            boost::ut::expect(result.error().size() == expected_error_count);
        }
    }
    else {
        const auto result = context.create_phase_supports<3>(snapshot_3d, empty_support_specifications());
        boost::ut::expect(not result.has_value());
        if (!result.has_value()) {
            boost::ut::expect(result.error().size() == expected_error_count);
        }
    }
}

void test_phase_graph_is_required(rift::RiftContext& context)
{
    const auto result = context.create_phase_supports<2>({}, {});
    boost::ut::expect(not result.has_value());
    if (result.has_value()) {
        return;
    }
    boost::ut::expect(result.error().size() == std::size_t{2});
    if (result.error().size() == 2) {
        boost::ut::expect(result.error().front().code == rift::PhaseSupportErrorCode::phase_graph_unavailable);
        boost::ut::expect(result.error().back().code == rift::PhaseSupportErrorCode::null_mesh);
    }
}

} // namespace

int main(int argc, char** argv)
{
    using namespace boost::ut;

    const auto* const test_mode = std::getenv("RIFT_PHASE_SUPPORT_TEST_MODE");
    auto run_without_phase_graph = false;
    if (test_mode != nullptr) {
        run_without_phase_graph = std::string_view{test_mode} == "no_phase_graph";
    }
    rift::RiftContext actual_context(argc, argv);

    // Boost.UT suites cannot capture runtime state during static registration.
    static rift::RiftContext* context_ptr = nullptr;
    context_ptr = &actual_context;
    if (run_without_phase_graph) {
        [[maybe_unused]] const suite<"PhaseSupportWithoutGraph"> suite = [] {
            "support creation requires a canonical phase graph"_test = [] {
                test_phase_graph_is_required(*context_ptr);
            };
        };
        const auto result = static_cast<int>(cfg<>.run());
        context_ptr = nullptr;
        return result;
    }

    const auto graph_result =
        actual_context.create_phase_graph(phase_graph_specification(), rift::interface_compatibility::accept_all);

    static bool graph_created = false;
    graph_created = graph_result.has_value();

    [[maybe_unused]] const suite<"PhaseSupportAgreement"> suite = [] {
        auto& context = *context_ptr;
        "distributed closure matches an independent global hypergraph fixed point"_test = [&context] {
            expect(graph_created);
            test_distributed_fixed_point<2>(context);
        };
        "distributed 3D closure matches an independent global hypergraph fixed point"_test = [&context] {
            expect(graph_created);
            test_distributed_fixed_point<3>(context);
        };
        "collective validation returns coherent asymmetric phase errors"_test = [&context] {
            test_collective_phase_errors(context);
        };
        "collective validation rejects a ghost-cell request on its nonowner"_test = [&context] {
            test_nonowner_request<2>(context);
        };
        "collective 3D validation rejects a ghost-cell request on its nonowner"_test = [&context] {
            test_nonowner_request<3>(context);
        };
        "collective validation requires one logical mesh snapshot"_test = [&context] {
            test_mesh_snapshot_agreement(context);
        };
        "collective validation requires one support dimension"_test = [&context] { test_dimension_agreement(context); };
        "collective validation reports a rank with no mesh"_test = [&context] { test_null_mesh_agreement(context); };
    };

    const auto result = static_cast<int>(cfg<>.run());
    context_ptr = nullptr;
    graph_created = false;
    return result;
}
