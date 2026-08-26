#pragma once

#include "../state_result_test_support.hpp"
#include "state_collective_test_support.hpp"

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
#include <utility>

namespace rift_test::mpi::state_factory_layout_context_00 {

inline void require_mpi(const int status)
{
    using namespace boost::ut;
    expect(status == MPI_SUCCESS) << "the MPI test fixture failed";
}

template<int dim>
std::pair<std::shared_ptr<const rift::MeshSnapshot<dim>>, rift::SupportEnvelope>
make_mesh(const rift::RunConfiguration& run, const MPI_Comm communicator)
{
    auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(communicator);
    dealii::GridGenerator::subdivided_hyper_cube(*triangulation, 2);
    rift::SupportEnvelope owned;
    for (const auto& cell : triangulation->active_cell_iterators()) {
        if (cell->is_locally_owned()) {
            owned.insert(cell->id());
        }
    }
    std::unique_ptr<dealii::Triangulation<dim>> consumed = std::move(triangulation);
    return {rift::test::require_state_result(rift::make_mesh_snapshot(run, std::move(consumed))), std::move(owned)};
}

template<int dim> void check_run_first_layout_agreement()
{
    using namespace boost::ut;
    const auto rank = dealii::Utilities::MPI::this_mpi_process(MPI_COMM_WORLD);
    auto run = rift::test::require_state_result(rift::RunConfiguration::create(MPI_COMM_WORLD));
    auto graph = rift::test::require_state_result(rift::make_phase_graph(run, {{"gas", "compressible"}}, {}));
    const auto gas =
        rift::test::require_state_result(graph.reference(rift::test::require_state_result(graph.find_phase("gas"))));

    MPI_Comm communicator_a = MPI_COMM_NULL;
    MPI_Comm communicator_b = MPI_COMM_NULL;
    require_mpi(MPI_Comm_dup(MPI_COMM_WORLD, &communicator_a));
    require_mpi(MPI_Comm_dup(MPI_COMM_WORLD, &communicator_b));
    auto [mesh_a, owned_a] = make_mesh<dim>(run, communicator_a);
    auto [mesh_b, owned_b] = make_mesh<dim>(run, communicator_b);
    require_mpi(MPI_Comm_free(&communicator_a));
    require_mpi(MPI_Comm_free(&communicator_b));

    const auto space_a = rift::test::make_distributed_state_space(mesh_a, graph, gas, owned_a);
    const auto space_b = rift::test::make_distributed_state_space(mesh_b, graph, gas, owned_b);
    const auto& selected = rank == 1U ? space_b.layout() : space_a.layout();
    const auto rejected = rift::make_state_store(selected, {});
    expect(!rejected.has_value());
    expect(rejected.error().code == rift::StateTransitionErrorCode::replicated_layout_mismatch);

    const auto retry_a = rift::make_state_store(space_a.layout(), {});
    const auto retry_b = rift::make_state_store(space_b.layout(), {});
    expect(retry_a.has_value());
    expect(retry_b.has_value());
    expect(retry_a->provenance().mesh == mesh_a->id());
    expect(retry_b->provenance().mesh == mesh_b->id());
}

inline void register_tests()
{
    using namespace boost::ut;
    "run-first store agreement rejects alternating congruent mesh contexts in 2D and 3D"_test = [] {
        check_run_first_layout_agreement<2>();
        check_run_first_layout_agreement<3>();
    };
}

} // namespace rift_test::mpi::state_factory_layout_context_00
