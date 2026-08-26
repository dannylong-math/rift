#pragma once

#include "../src/mesh_snapshot_internal.hpp"
#include "../src/run_configuration_internal.hpp"
#include "mesh_snapshot_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <new>
#include <vector>

namespace rift_test::mesh_snapshot_mpi_failures_00 {

inline int fail_inter_query([[maybe_unused]] MPI_Comm communicator, [[maybe_unused]] int* value)
{
    return MPI_ERR_OTHER;
}
inline int fail_size([[maybe_unused]] MPI_Comm communicator, [[maybe_unused]] int* value) { return MPI_ERR_OTHER; }

inline std::vector<int> fail_rank_allocation([[maybe_unused]] int size) { throw std::bad_alloc{}; }

inline int fail_group([[maybe_unused]] MPI_Comm communicator, [[maybe_unused]] MPI_Group* group)
{
    return MPI_ERR_OTHER;
}

inline MPI_Group& first_group()
{
    static MPI_Group group = MPI_GROUP_NULL;
    return group;
}
inline int fail_second_group(const MPI_Comm communicator, MPI_Group* group)
{
    if (communicator != MPI_COMM_WORLD) {
        const int status = MPI_Comm_group(communicator, group);
        first_group() = *group;
        return status;
    }
    if (first_group() != MPI_GROUP_NULL) {
        MPI_Group_free(&first_group());
    }
    return MPI_ERR_OTHER;
}
inline int fail_translation(MPI_Group /*source*/, int /*count*/, const int* /*source_ranks*/, MPI_Group /*destination*/,
                            int* /*destination_ranks*/)
{
    return MPI_ERR_OTHER;
}

inline int fail_free(MPI_Group* group)
{
    const int cleanup = MPI_Group_free(group);
    return cleanup == MPI_SUCCESS ? MPI_ERR_OTHER : cleanup;
}

inline int& free_call_count()
{
    static int count = 0;
    return count;
}
inline int fail_second_free(MPI_Group* group)
{
    ++free_call_count();
    const int cleanup = MPI_Group_free(group);
    return cleanup == MPI_SUCCESS && free_call_count() == 2 ? MPI_ERR_OTHER : cleanup;
}

inline int fail_compare([[maybe_unused]] MPI_Comm first, [[maybe_unused]] MPI_Comm second,
                        [[maybe_unused]] int* relationship)
{
    return MPI_ERR_OTHER;
}

inline void expect_fatal(rift::detail::MeshSnapshotMpiOperations operations)
{
    using namespace boost::ut;
    const auto run = mesh_snapshot_test::make_run();
    try {
        [[maybe_unused]] const auto unexpected = rift::detail::classify_mesh_communicator(
            rift::detail::RunConfigurationAccess::control(run), MPI_COMM_SELF, operations);
        expect(false);
    }
    catch (const mesh_snapshot_test::FatalMpiFailure& failure) {
        expect(failure.status == MPI_ERR_OTHER || failure.status == MPI_ERR_NO_MEM);
    }
}

} // namespace rift_test::mesh_snapshot_mpi_failures_00

namespace rift_test::mesh_snapshot_mpi_failures_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "communicator query and local allocation failures are fatal"_test = [] {
        auto operations = mesh_snapshot_test::operations();
        operations.test_intercommunicator = fail_inter_query;
        expect_fatal(operations);

        operations = mesh_snapshot_test::operations();
        operations.communicator_size = fail_size;
        expect_fatal(operations);

        operations = mesh_snapshot_test::operations();
        operations.allocate_rank_buffer = fail_rank_allocation;
        expect_fatal(operations);
    };

    "group translation and cleanup failures are fatal"_test = [] {
        auto operations = mesh_snapshot_test::operations();
        operations.communicator_group = fail_group;
        expect_fatal(operations);

        operations = mesh_snapshot_test::operations();
        operations.translate_ranks = fail_translation;
        expect_fatal(operations);

        operations = mesh_snapshot_test::operations();
        operations.free_group = fail_free;
        expect_fatal(operations);

        operations = mesh_snapshot_test::operations();
        first_group() = MPI_GROUP_NULL;
        operations.communicator_group = fail_second_group;
        expect_fatal(operations);

        operations = mesh_snapshot_test::operations();
        free_call_count() = 0;
        operations.free_group = fail_second_free;
        expect_fatal(operations);
    };

    "communicator comparison failure is fatal"_test = [] {
        auto operations = mesh_snapshot_test::operations();
        operations.compare = fail_compare;
        expect_fatal(operations);
    };
}

} // namespace rift_test::mesh_snapshot_mpi_failures_00
