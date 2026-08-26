#pragma once

#include "../src/mesh_snapshot_internal.hpp"
#include "../src/run_configuration_internal.hpp"
#include "mesh_snapshot_test_support.hpp"

#include <boost/ut.hpp>
#include <mpi.h>

namespace rift_test::mesh_snapshot_cleanup_failures_00 {

inline int& cleanup_count()
{
    static int count = 0;
    return count;
}

inline int report_initialized(int* value)
{
    *value = 1;
    return MPI_SUCCESS;
}

inline int report_uninitialized(int* value)
{
    *value = 0;
    return MPI_SUCCESS;
}

inline int report_finalized(int* value)
{
    *value = 1;
    return MPI_SUCCESS;
}

inline int report_not_finalized(int* value)
{
    *value = 0;
    return MPI_SUCCESS;
}

inline int fail_query([[maybe_unused]] int* value) { return MPI_ERR_OTHER; }

inline int observe_free(MPI_Comm* communicator)
{
    ++cleanup_count();
    *communicator = MPI_COMM_NULL;
    return MPI_SUCCESS;
}

inline int fail_free([[maybe_unused]] MPI_Comm* communicator) { return MPI_ERR_OTHER; }

inline void expect_fatal(const rift::detail::MpiCleanupOperations operations)
{
    using namespace boost::ut;
    MPI_Comm communicator = MPI_COMM_SELF;
    try {
        rift::detail::release_mesh_communicator_with_operations(communicator, operations);
        expect(false);
    }
    catch (const mesh_snapshot_test::FatalMpiFailure& failure) {
        expect(failure.status == MPI_ERR_OTHER);
    }
}

} // namespace rift_test::mesh_snapshot_cleanup_failures_00

namespace rift_test::mesh_snapshot_cleanup_failures_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "mesh cleanup MPI failures are fatal"_test = [] {
        expect_fatal({.initialized = fail_query,
                      .finalized = report_not_finalized,
                      .free_communicator = observe_free,
                      .abort = mesh_snapshot_test::throw_abort});
        expect_fatal({.initialized = report_initialized,
                      .finalized = fail_query,
                      .free_communicator = observe_free,
                      .abort = mesh_snapshot_test::throw_abort});
        expect_fatal({.initialized = report_initialized,
                      .finalized = report_not_finalized,
                      .free_communicator = fail_free,
                      .abort = mesh_snapshot_test::throw_abort});
    };

    "mesh cleanup skips inactive MPI and otherwise releases once"_test = [] {
        MPI_Comm communicator = MPI_COMM_SELF;
        cleanup_count() = 0;
        rift::detail::release_mesh_communicator_with_operations(communicator,
                                                                {.initialized = report_uninitialized,
                                                                 .finalized = report_not_finalized,
                                                                 .free_communicator = observe_free,
                                                                 .abort = mesh_snapshot_test::throw_abort});
        rift::detail::release_mesh_communicator_with_operations(communicator,
                                                                {.initialized = report_initialized,
                                                                 .finalized = report_finalized,
                                                                 .free_communicator = observe_free,
                                                                 .abort = mesh_snapshot_test::throw_abort});
        rift::detail::release_mesh_communicator_with_operations(communicator,
                                                                {.initialized = report_initialized,
                                                                 .finalized = report_not_finalized,
                                                                 .free_communicator = observe_free,
                                                                 .abort = mesh_snapshot_test::throw_abort});
        expect(cleanup_count() == 1_i);
        expect(communicator == MPI_COMM_NULL);
    };
}

} // namespace rift_test::mesh_snapshot_cleanup_failures_00
