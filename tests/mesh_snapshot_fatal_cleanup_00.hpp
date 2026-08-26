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
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <new>
#include <rift/mesh_snapshot.hpp>
#include <rift/run_configuration.hpp>
#include <utility>

namespace rift_test::mesh_snapshot_fatal_cleanup_00 {

inline int& duplicate_calls()
{
    static int count = 0;
    return count;
}

inline int& free_calls()
{
    static int count = 0;
    return count;
}

inline int report_initialized(int* value)
{
    *value = 1;
    return MPI_SUCCESS;
}

inline int report_not_finalized(int* value)
{
    *value = 0;
    return MPI_SUCCESS;
}

inline int observe_duplicate(const MPI_Comm source, MPI_Comm* duplicate)
{
    ++duplicate_calls();
    *duplicate = source;
    return MPI_SUCCESS;
}

inline int observe_free(MPI_Comm* communicator)
{
    ++free_calls();
    *communicator = MPI_COMM_NULL;
    return MPI_SUCCESS;
}

inline int observe_abort([[maybe_unused]] MPI_Comm communicator, const int status)
{
    if (free_calls() != 0) {
        throw mesh_snapshot_test::FatalMpiFailure{-1};
    }
    throw mesh_snapshot_test::FatalMpiFailure{status};
}

inline std::shared_ptr<const rift::detail::MeshCommunicatorControl>
allocate_observed_control(const MPI_Comm communicator, [[maybe_unused]] rift::detail::MpiAbort abort)
{
    return std::make_shared<rift::detail::MeshCommunicatorControl>(
        communicator, rift::detail::MpiCleanupOperations{.initialized = report_initialized,
                                                         .finalized = report_not_finalized,
                                                         .free_communicator = observe_free,
                                                         .abort = observe_abort});
}

inline rift::RunConfiguration make_observed_run()
{
    MPI_Comm duplicate = MPI_COMM_NULL;
    if (MPI_Comm_dup(MPI_COMM_SELF, &duplicate) != MPI_SUCCESS) {
        throw mesh_snapshot_test::FatalMpiFailure{MPI_ERR_OTHER};
    }
    auto control = std::make_shared<const rift::detail::RunConfigurationControl>(
        duplicate, rift::RunConfigurationId::from_index(0), 0, observe_abort);
    return rift::detail::RunConfigurationAccess::adopt(std::move(control));
}

inline rift::detail::RetainedMeshCommunicator
make_observed_communicator(const std::shared_ptr<const rift::detail::RunConfigurationControl>& run_control)
{
    return rift::detail::own_mesh_communicator_with_operations(run_control, MPI_COMM_SELF, observe_duplicate,
                                                               allocate_observed_control);
}

template<int dim>
std::unique_ptr<dealii::Triangulation<dim>>
fail_distributed_allocation([[maybe_unused]] MPI_Comm communicator,
                            [[maybe_unused]] const dealii::Triangulation<dim>& source)
{
    throw std::bad_alloc{};
}

template<int dim>
std::unique_ptr<dealii::Triangulation<dim>>
fail_distributed_copy([[maybe_unused]] MPI_Comm communicator, [[maybe_unused]] const dealii::Triangulation<dim>& source)
{
    throw 7;
}

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
void expect_rebuild_fatal_without_free(const rift::detail::DistributedTriangulationBuilder<dim> builder,
                                       const int expected_status)
{
    using namespace boost::ut;
    duplicate_calls() = 0;
    free_calls() = 0;
    const auto run = make_observed_run();
    const auto control = rift::detail::RunConfigurationAccess::control(run);
    auto retained = make_observed_communicator(control);
    std::unique_ptr<dealii::Triangulation<dim>> mesh =
        std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_SELF);
    dealii::GridGenerator::hyper_cube(*mesh);

    try {
        static_cast<void>(rift::detail::rebuild_distributed_mesh_with_builder(control, std::move(mesh),
                                                                              std::move(retained), builder));
        expect(false);
    }
    catch (const mesh_snapshot_test::FatalMpiFailure& failure) {
        expect(failure.status == expected_status);
        expect(duplicate_calls() == 1);
        expect(free_calls() == 0);
    }
}

template<int dim> void expect_snapshot_fatal_without_free()
{
    using namespace boost::ut;
    duplicate_calls() = 0;
    free_calls() = 0;
    const auto run = make_observed_run();
    const auto control = rift::detail::RunConfigurationAccess::control(run);
    auto retained = make_observed_communicator(control);
    auto mesh = std::make_unique<dealii::Triangulation<dim>>();
    dealii::GridGenerator::hyper_cube(*mesh);
    rift::detail::MeshSnapshotStorage<dim> storage{.communicator_control = std::move(retained.control),
                                                   .triangulation = std::move(mesh),
                                                   .communicator = retained.communicator};

    try {
        static_cast<void>(rift::detail::allocate_mesh_snapshot_with_allocator(
            control, std::move(storage), {.run = control->id(), .mesh = rift::MeshSnapshotId::from_index(0)},
            fail_snapshot_allocation<dim>));
        expect(false);
    }
    catch (const mesh_snapshot_test::FatalMpiFailure& failure) {
        expect(failure.status == MPI_ERR_NO_MEM);
        expect(duplicate_calls() == 1);
        expect(free_calls() == 0);
    }
}

} // namespace rift_test::mesh_snapshot_fatal_cleanup_00

namespace rift_test::mesh_snapshot_fatal_cleanup_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "rank-local distributed rebuild failure never frees collectively"_test = [] {
        expect_rebuild_fatal_without_free<2>(fail_distributed_allocation<2>, MPI_ERR_NO_MEM);
        expect_rebuild_fatal_without_free<2>(fail_distributed_copy<2>, MPI_ERR_OTHER);
        expect_rebuild_fatal_without_free<3>(fail_distributed_allocation<3>, MPI_ERR_NO_MEM);
        expect_rebuild_fatal_without_free<3>(fail_distributed_copy<3>, MPI_ERR_OTHER);
    };

    "rank-local snapshot allocation failure never frees collectively"_test = [] {
        expect_snapshot_fatal_without_free<2>();
        expect_snapshot_fatal_without_free<3>();
    };
}

} // namespace rift_test::mesh_snapshot_fatal_cleanup_00
