#pragma once

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <memory>
#include <mpi.h>
#include <mpi_proto.h>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rift_test::mpi::space_registry_caller_communicator_lifetime_00 {

template<int dim> void check_caller_communicator_lifetime()
{
    using namespace boost::ut;
    MPI_Comm caller = MPI_COMM_NULL;
    expect(MPI_Comm_dup(MPI_COMM_WORLD, &caller) == MPI_SUCCESS);
    auto run = rift::RunConfiguration::create(caller).value();
    auto graph = rift::make_phase_graph(run, {{"gas", "compressible"}}, {}).value();
    const auto gas_id = graph.find_phase("gas");
    if (!gas_id.has_value()) {
        throw std::logic_error("the communicator-lifetime fixture did not contain gas");
    }
    const auto gas = graph.reference(*gas_id).value();
    auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(caller);
    dealii::GridGenerator::subdivided_hyper_cube(*triangulation, 2);
    rift::SupportEnvelope owned;
    for (const auto& cell : triangulation->active_cell_iterators()) {
        if (cell->is_locally_owned()) {
            owned.insert(cell->id());
        }
    }
    std::unique_ptr<dealii::Triangulation<dim>> consumed = std::move(triangulation);
    auto mesh = rift::make_mesh_snapshot(run, std::move(consumed)).value();
    expect(MPI_Comm_free(&caller) == MPI_SUCCESS);
    rift::SpaceRegistry<dim> const registry(mesh);
    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = gas, .name = "flow", .components = dim + 2, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1}};
    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = gas, .mesh = mesh->id(), .locally_owned_requested_cells = std::move(owned)}};
    auto draft = registry.begin_draft(graph, std::move(specification), std::move(supports));
    expect(draft.has_value());
    auto snapshot = registry.finalize(*draft, {});
    expect(snapshot.has_value());
}

inline void register_tests()
{
    using namespace boost::ut;
    "registry snapshots outlive the caller communicator handle in 2D and 3D"_test = [] {
        check_caller_communicator_lifetime<2>();
        check_caller_communicator_lifetime<3>();
    };
}

} // namespace rift_test::mpi::space_registry_caller_communicator_lifetime_00
