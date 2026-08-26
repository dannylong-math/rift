#pragma once

#include "../src/mesh_snapshot_internal.hpp"
#include "../src/run_configuration_internal.hpp"
#include "mesh_snapshot_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <memory>
#include <mpi.h>
#include <new>
#include <rift/mesh_snapshot.hpp>
#include <utility>

namespace rift_test::mesh_snapshot_allocation_failures_00 {

template<int dim>
std::shared_ptr<const rift::MeshSnapshot<dim>> fail_snapshot_allocation(
    [[maybe_unused]] std::shared_ptr<const rift::detail::RunConfigurationControl> run_control,
    [[maybe_unused]] std::shared_ptr<const rift::detail::MeshCommunicatorControl> communicator_control,
    [[maybe_unused]] std::unique_ptr<dealii::Triangulation<dim>> triangulation,
    [[maybe_unused]] rift::MeshSnapshotProvenance provenance, [[maybe_unused]] MPI_Comm communicator)
{
    [[maybe_unused]] auto consumed_run_control = std::move(run_control);
    [[maybe_unused]] auto consumed_communicator_control = std::move(communicator_control);
    throw std::bad_alloc{};
}

template<int dim>
rift::detail::MeshSnapshotStorage<dim> fail_reconstruction_allocation(
    [[maybe_unused]] const std::shared_ptr<const rift::detail::RunConfigurationControl>& run_control,
    [[maybe_unused]] std::unique_ptr<dealii::Triangulation<dim>> triangulation, [[maybe_unused]] MPI_Comm communicator)
{
    throw std::bad_alloc{};
}

template<int dim>
rift::detail::MeshSnapshotStorage<dim>
fail_reconstruction([[maybe_unused]] const std::shared_ptr<const rift::detail::RunConfigurationControl>& run_control,
                    [[maybe_unused]] std::unique_ptr<dealii::Triangulation<dim>> triangulation,
                    [[maybe_unused]] MPI_Comm communicator)
{
    throw 7;
}

template<int dim>
void expect_reconstruction_fatal(const rift::detail::DistributedMeshRebuilder<dim> rebuild, const int expected_status)
{
    using namespace boost::ut;
    const auto run = mesh_snapshot_test::make_run();
    std::unique_ptr<dealii::Triangulation<dim>> mesh =
        std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_SELF);
    dealii::GridGenerator::hyper_cube(*mesh);
    try {
        static_cast<void>(rift::detail::stabilize_mesh_storage_with_rebuilder(
            rift::detail::RunConfigurationAccess::control(run), std::move(mesh), MPI_COMM_SELF, rebuild));
        expect(false);
    }
    catch (const mesh_snapshot_test::FatalMpiFailure& failure) {
        expect(failure.status == expected_status);
    }
}

} // namespace rift_test::mesh_snapshot_allocation_failures_00

namespace rift_test::mesh_snapshot_allocation_failures_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "post-collective snapshot allocation failure is fatal"_test = [] {
        const auto run = mesh_snapshot_test::make_run();
        auto mesh = std::make_unique<dealii::Triangulation<2>>();
        dealii::GridGenerator::hyper_cube(*mesh);
        try {
            [[maybe_unused]] const auto unexpected =
                rift::detail::make_mesh_snapshot_with_allocator(run, std::move(mesh), fail_snapshot_allocation<2>);
            expect(false);
        }
        catch (const mesh_snapshot_test::FatalMpiFailure& failure) {
            expect(failure.status == MPI_ERR_NO_MEM);
        }
    };

    "distributed reconstruction failures are fatal before another collective"_test = [] {
        expect_reconstruction_fatal<2>(fail_reconstruction_allocation<2>, MPI_ERR_NO_MEM);
        expect_reconstruction_fatal<2>(fail_reconstruction<2>, MPI_ERR_OTHER);
        expect_reconstruction_fatal<3>(fail_reconstruction_allocation<3>, MPI_ERR_NO_MEM);
        expect_reconstruction_fatal<3>(fail_reconstruction<3>, MPI_ERR_OTHER);
    };
}

} // namespace rift_test::mesh_snapshot_allocation_failures_00
