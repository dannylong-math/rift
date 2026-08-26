#pragma once

#include "../discrete_state_test_support.hpp"
#include "../state_result_test_support.hpp"
#include "state_collective_test_support.hpp"

#include <algorithm>
#include <boost/ut.hpp>
#include <cstdint>
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
#include <set>
#include <utility>
#include <vector>

namespace rift_test::mpi::state_global_ids_00 {

template<class Id> void expect_communicator_agreement(const MPI_Comm communicator, const Id id)
{
    using namespace boost::ut;
    const auto gathered = dealii::Utilities::MPI::all_gather(communicator, id.value());
    expect(std::ranges::all_of(gathered, [&](const auto value) { return value == gathered.front(); }));
}

template<int dim> void check_overlapping_communicator_ids()
{
    using namespace boost::ut;
    const auto rank = dealii::Utilities::MPI::this_mpi_process(MPI_COMM_WORLD);
    const auto size = dealii::Utilities::MPI::n_mpi_processes(MPI_COMM_WORLD);
    const auto world = rift::test::make_distributed_state_fixture<dim>();
    auto world_store = rift::test::require_state_result(rift::make_state_store(world.space.layout(), {}));

    MPI_Comm reversed = MPI_COMM_NULL;
    expect(MPI_Comm_split(MPI_COMM_WORLD, 0, static_cast<int>(size - rank), &reversed) == MPI_SUCCESS);
    auto reversed_run = rift::test::require_state_result(rift::RunConfiguration::create(reversed));
    auto reversed_graph =
        rift::test::require_state_result(rift::make_phase_graph(reversed_run, {{"gas", "compressible"}}, {}));
    const auto reversed_gas = rift::test::require_state_result(
        reversed_graph.reference(rift::test::require_state_result(reversed_graph.find_phase("gas"))));
    auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(reversed);
    dealii::GridGenerator::subdivided_hyper_cube(*triangulation, 2);
    rift::SupportEnvelope owned;
    for (const auto& cell : triangulation->active_cell_iterators()) {
        if (cell->is_locally_owned()) {
            owned.insert(cell->id());
        }
    }
    std::unique_ptr<dealii::Triangulation<dim>> consumed = std::move(triangulation);
    auto reversed_mesh = rift::test::require_state_result(rift::make_mesh_snapshot(reversed_run, std::move(consumed)));
    expect(MPI_Comm_free(&reversed) == MPI_SUCCESS);
    const auto reversed_space =
        rift::test::make_distributed_state_space(reversed_mesh, reversed_graph, reversed_gas, owned);
    auto reversed_store = rift::test::require_state_result(rift::make_state_store(reversed_space.layout(), {}));

    const auto self_space = rift::test::make_space_with_one_phase_field<dim>();
    auto self_store = rift::test::require_state_result(rift::make_state_store(self_space.layout(), {}));

    expect_communicator_agreement(MPI_COMM_WORLD, world_store.id());
    expect_communicator_agreement(reversed_run.communicator(), reversed_store.id());
    const auto world_store_ids = dealii::Utilities::MPI::all_gather(MPI_COMM_WORLD, world_store.id().value());
    const auto reversed_store_ids = dealii::Utilities::MPI::all_gather(MPI_COMM_WORLD, reversed_store.id().value());
    const auto self_store_ids = dealii::Utilities::MPI::all_gather(MPI_COMM_WORLD, self_store.id().value());
    const auto world_snapshot_ids = dealii::Utilities::MPI::all_gather(
        MPI_COMM_WORLD, world_store.snapshot(rift::StateSlot::accepted).stamp().snapshot.value());
    const auto reversed_snapshot_ids = dealii::Utilities::MPI::all_gather(
        MPI_COMM_WORLD, reversed_store.snapshot(rift::StateSlot::accepted).stamp().snapshot.value());
    const auto self_snapshot_ids = dealii::Utilities::MPI::all_gather(
        MPI_COMM_WORLD, self_store.snapshot(rift::StateSlot::accepted).stamp().snapshot.value());

    std::set<std::uint64_t> stores(self_store_ids.begin(), self_store_ids.end());
    stores.insert(world_store_ids.front());
    stores.insert(reversed_store_ids.front());
    std::set<std::uint64_t> snapshots(self_snapshot_ids.begin(), self_snapshot_ids.end());
    snapshots.insert(world_snapshot_ids.front());
    snapshots.insert(reversed_snapshot_ids.front());
    expect(stores.size() == self_store_ids.size() + 2U);
    expect(snapshots.size() == self_snapshot_ids.size() + 2U);
}

inline void register_tests()
{
    using namespace boost::ut;
    "state store and snapshot IDs stay unique across disjoint, world, and reversed communicators"_test = [] {
        check_overlapping_communicator_ids<2>();
        check_overlapping_communicator_ids<3>();
    };
}

} // namespace rift_test::mpi::state_global_ids_00
