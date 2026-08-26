#pragma once

#include "../state_result_test_support.hpp"

#include <deal.II/distributed/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <memory>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <string>
#include <utility>
#include <vector>

namespace rift::test {

template<int dim>
SpaceSnapshot<dim> make_distributed_state_space(const std::shared_ptr<const MeshSnapshot<dim>>& mesh,
                                                const PhaseGraph& graph, const PhaseReference gas,
                                                const SupportEnvelope& owned, std::string name = "flow",
                                                std::vector<RegionalEntrySpecification> regional_entries = {})
{
    SpaceRegistry<dim> const registry(mesh);
    SpaceSpecification specification{
        .phase_fields = {{.phase = gas, .name = std::move(name), .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };
    std::vector<PhaseSupportSpecification> supports{
        {.phase = gas, .mesh = mesh->id(), .locally_owned_requested_cells = owned}};
    auto draft = require_state_result(registry.begin_draft(graph, std::move(specification), std::move(supports)));
    return require_state_result(registry.finalize(draft, std::move(regional_entries)));
}

template<int dim> struct DistributedStateFixture {
    DistributedStateFixture(RunConfiguration run_value, PhaseGraph graph_value,
                            std::shared_ptr<const MeshSnapshot<dim>> mesh_value, const PhaseReference gas_value,
                            SupportEnvelope owned_value, SpaceSnapshot<dim> space_value) :
        run(std::move(run_value)),
        graph(std::move(graph_value)),
        mesh(std::move(mesh_value)),
        gas(gas_value),
        owned(std::move(owned_value)),
        space(std::move(space_value))
    {
    }

    RunConfiguration run;
    PhaseGraph graph;
    std::shared_ptr<const MeshSnapshot<dim>> mesh;
    PhaseReference gas;
    SupportEnvelope owned;
    SpaceSnapshot<dim> space;
};

template<int dim>
DistributedStateFixture<dim>
make_distributed_state_fixture(std::vector<RegionalEntrySpecification> regional_entries = {})
{
    auto run = require_state_result(RunConfiguration::create(MPI_COMM_WORLD));
    auto graph = require_state_result(make_phase_graph(run, {{"gas", "compressible"}}, {}));
    const auto gas = require_state_result(graph.reference(require_state_result(graph.find_phase("gas"))));
    auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_WORLD);
    dealii::GridGenerator::subdivided_hyper_cube(*triangulation, 2);
    SupportEnvelope owned;
    for (const auto& cell : triangulation->active_cell_iterators()) {
        if (cell->is_locally_owned()) {
            owned.insert(cell->id());
        }
    }
    std::unique_ptr<dealii::Triangulation<dim>> consumed = std::move(triangulation);
    auto mesh = require_state_result(make_mesh_snapshot(run, std::move(consumed)));
    auto space = make_distributed_state_space(mesh, graph, gas, owned, "flow", std::move(regional_entries));
    return {std::move(run), std::move(graph), std::move(mesh), gas, std::move(owned), std::move(space)};
}

template<int dim> StateFieldReference flow_reference(const SpaceSnapshot<dim>& space)
{
    return require_state_result(space.layout().field_reference(space.field_spaces().front().id()));
}

} // namespace rift::test
