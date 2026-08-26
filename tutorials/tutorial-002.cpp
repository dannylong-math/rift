#include <algorithm>
#include <deal.II/base/mpi.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <iostream>
#include <memory>
#include <mpi.h>
#include <optional>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

/**
 * \brief Let every rank agree whether a tutorial invariant holds.
 * \param[in] local_condition Whether the invariant holds on this rank.
 * \param[in] message Explanation printed by rank zero when any rank fails.
 * \return `true` only when the condition holds on every rank.
 */
bool collective_check(const bool local_condition, const std::string_view message)
{
    const int local_value = local_condition ? 1 : 0;
    const int global_value = dealii::Utilities::MPI::min(local_value, MPI_COMM_WORLD);
    const auto rank = dealii::Utilities::MPI::this_mpi_process(MPI_COMM_WORLD);
    if (global_value == 0 && rank == 0) {
        std::cerr << "Tutorial 2 failed: " << message << '\n';
    }
    return global_value != 0;
}

/**
 * \brief Print finite-element space construction errors once for the MPI job.
 * \param[in] errors The collectively agreed space diagnostics.
 */
void print_space_errors(const rift::SpaceBuildErrors& errors)
{
    const auto rank = dealii::Utilities::MPI::this_mpi_process(MPI_COMM_WORLD);
    if (rank == 0) {
        for (const auto& error : errors) {
            std::cerr << error.message << '\n';
        }
    }
}

/** \brief Retain the exact owner-local requests produced by the teaching classifier. */
struct OwnerLocalSupports {
    /** \brief Owner-local cells requested for the gas phase. */
    rift::SupportEnvelope gas;
    /** \brief Owner-local cells requested for the liquid phase. */
    rift::SupportEnvelope liquid;
};

/**
 * \brief Partition every owner-local active cell by its center coordinate.
 * \param[in] mesh Immutable distributed mesh whose owner cells are classified.
 * \return Two globally nonempty exact owner-cell partitions, or no value on failure.
 */
std::optional<OwnerLocalSupports> classify_owner_local_support(const rift::MeshSnapshot<2>& mesh)
{
    // rift:snippet-begin tutorial-002.owner-local-support
    OwnerLocalSupports result;
    rift::SupportEnvelope locally_owned_cells;
    for (const auto& cell : mesh.triangulation().active_cell_iterators()) {
        if (!cell->is_locally_owned()) {
            continue;
        }
        locally_owned_cells.insert(cell->id());
        auto& support = cell->center()(0) < 0.5 ? result.liquid : result.gas;
        support.insert(cell->id());
    }

    bool local_partition_is_exact = result.gas.size() + result.liquid.size() == locally_owned_cells.size();
    for (const auto& cell : locally_owned_cells) {
        local_partition_is_exact =
            local_partition_is_exact && (result.gas.contains(cell) != result.liquid.contains(cell));
    }
    bool local_classifier_matches_geometry = true;
    for (const auto& cell : mesh.triangulation().active_cell_iterators()) {
        if (!cell->is_locally_owned()) {
            continue;
        }
        const bool expected_liquid = cell->center()(0) < 0.5;
        local_classifier_matches_geometry = local_classifier_matches_geometry &&
                                            result.liquid.contains(cell->id()) == expected_liquid &&
                                            result.gas.contains(cell->id()) == !expected_liquid;
    }
    const auto global_gas_cells =
        dealii::Utilities::MPI::sum(static_cast<unsigned int>(result.gas.size()), MPI_COMM_WORLD);
    const auto global_liquid_cells =
        dealii::Utilities::MPI::sum(static_cast<unsigned int>(result.liquid.size()), MPI_COMM_WORLD);
    if (!collective_check(local_partition_is_exact && local_classifier_matches_geometry && global_gas_cells > 0U &&
                              global_liquid_cells > 0U,
                          "the center classifier did not form two nonempty exact owner-cell partitions")) {
        return std::nullopt;
    }
    return result;
    // rift:snippet-end tutorial-002.owner-local-support
}

/**
 * \brief Check that final closed support contains every originally requested cell.
 * \param[in] support Final phase support after closure.
 * \param[in] requested Original owner-local support request.
 * \return `true` when `requested` is a subset of the support's final mask.
 */
bool request_is_retained(const rift::PhaseSupport& support, const rift::SupportEnvelope& requested)
{
    return std::ranges::all_of(
        requested, [&support](const auto& cell) { return support.final_locally_owned_cells().contains(cell); });
}

/**
 * \brief Run the distributed mesh and finite-element space lesson.
 * \return Zero after the complete workflow succeeds on every rank.
 */
int run_tutorial()
{
    // rift:snippet-begin tutorial-002.describe-run
    auto run_result = rift::RunConfiguration::create(MPI_COMM_WORLD);
    if (!collective_check(run_result.has_value(), "Rift could not create the run configuration")) {
        return 1;
    }
    auto run = std::move(*run_result);

    const rift::InterfaceCompatibilityCheck accept_available_pair =
        [](const rift::PhaseDescriptor&, const rift::PhaseDescriptor&,
           const rift::InterfaceSpecification&) -> std::optional<std::string> { return std::nullopt; };
    auto graph_result =
        rift::make_phase_graph(run, {{"gas", "compressible"}, {"liquid", "low-mach"}},
                               {{"free_surface", "liquid", "gas", "finite-rate"}}, accept_available_pair);
    if (!collective_check(graph_result.has_value(), "the phase graph input was rejected")) {
        return 1;
    }
    auto graph = std::move(*graph_result);
    // rift:snippet-end tutorial-002.describe-run

    const auto gas_id = graph.find_phase("gas");
    const auto liquid_id = graph.find_phase("liquid");
    if (!collective_check(gas_id && liquid_id, "the phase names were not found")) {
        return 1;
    }
    if (!gas_id || !liquid_id) {
        return 1;
    }
    const auto gas = graph.reference(*gas_id);
    const auto liquid = graph.reference(*liquid_id);
    if (!collective_check(gas && liquid, "phase references could not be created")) {
        return 1;
    }

    // rift:snippet-begin tutorial-002.distributed-mesh
    auto distributed = std::make_unique<dealii::parallel::distributed::Triangulation<2>>(MPI_COMM_WORLD);
    dealii::GridGenerator::subdivided_hyper_cube(*distributed, 2);
    std::unique_ptr<dealii::Triangulation<2>> mesh_input = std::move(distributed);

    auto mesh_result = rift::make_mesh_snapshot(run, std::move(mesh_input));
    if (!collective_check(mesh_result.has_value(), "the distributed mesh could not be retained")) {
        return 1;
    }
    const auto& mesh = *mesh_result;
    // rift:snippet-end tutorial-002.distributed-mesh

    auto classified_support = classify_owner_local_support(*mesh);
    if (!classified_support) {
        return 1;
    }
    const auto requested_gas_cells = classified_support->gas;
    const auto requested_liquid_cells = classified_support->liquid;

    // rift:snippet-begin tutorial-002.support-records
    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = *gas, .mesh = mesh->id(), .locally_owned_requested_cells = std::move(classified_support->gas)},
        {.phase = *liquid, .mesh = mesh->id(), .locally_owned_requested_cells = std::move(classified_support->liquid)},
    };
    // rift:snippet-end tutorial-002.support-records

    // rift:snippet-begin tutorial-002.build-spaces
    rift::SpaceSpecification specification{
        .phase_fields =
            {
                {.phase = *gas, .name = "gas_state", .components = 4, .polynomial_degree = 1},
                {.phase = *liquid, .name = "liquid_state", .components = 4, .polynomial_degree = 1},
            },
        .level_set = {.name = "level_set", .components = 1, .polynomial_degree = 1},
    };

    const rift::SpaceRegistry<2> registry(mesh);
    auto draft_result = registry.begin_draft(graph, std::move(specification), std::move(supports));
    if (!collective_check(draft_result.has_value(), "the finite-element space draft was rejected")) {
        if (!draft_result) {
            print_space_errors(draft_result.error());
        }
        return 1;
    }

    auto space_result = registry.finalize(*draft_result, {});
    if (!collective_check(space_result.has_value(), "the state layout could not be finalized")) {
        if (!space_result) {
            print_space_errors(space_result.error());
        }
        return 1;
    }
    const auto space = std::move(*space_result);
    // rift:snippet-end tutorial-002.build-spaces

    // rift:snippet-begin tutorial-002.inspect-spaces
    const auto gas_field_id = space.find_field(*gas, "gas_state");
    const auto liquid_field_id = space.find_field(*liquid, "liquid_state");
    if (!collective_check(gas_field_id && liquid_field_id, "the finalized fields were not found") || !gas_field_id ||
        !liquid_field_id) {
        return 1;
    }
    const auto& final_gas_support = space.field_space(*gas, *gas_field_id).support();
    const auto& final_liquid_support = space.field_space(*liquid, *liquid_field_id).support();
    const bool requests_are_retained = request_is_retained(final_gas_support, requested_gas_cells) &&
                                       request_is_retained(final_liquid_support, requested_liquid_cells);
    const bool layout_is_expected = mesh->triangulation().n_global_active_cells() == 4 &&
                                    space.field_spaces().size() == 2 && space.layout().field_blocks().size() == 3 &&
                                    space.layout().regional_entries().empty() && requests_are_retained &&
                                    space.field_space(*gas, *gas_field_id).dof_handler().n_dofs() > 0 &&
                                    space.field_space(*liquid, *liquid_field_id).dof_handler().n_dofs() > 0 &&
                                    space.level_set_space().dof_handler().n_dofs() > 0;
    if (!collective_check(layout_is_expected, "the finalized spaces do not match the requested fields")) {
        return 1;
    }
    // rift:snippet-end tutorial-002.inspect-spaces

    const auto rank = dealii::Utilities::MPI::this_mpi_process(MPI_COMM_WORLD);
    if (rank == 0) {
        std::cout << "Tutorial 2 complete: 4 cells and 3 distributed field blocks.\n";
    }
    return 0;
}

} // namespace

/**
 * \brief Keep the MPI lifetime outside every object created by Tutorial 2.
 * \param[in] argc Number of command-line arguments.
 * \param[in] argv Command-line arguments forwarded to the MPI runtime.
 * \return The collective tutorial result.
 */
int main(int argc, char** argv)
{
    const dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    return run_tutorial();
}
