#pragma once

#include "../src/run_configuration_internal.hpp"

#include <boost/ut.hpp>
#include <mpi.h>

namespace rift_test::run_configuration_cleanup_failures_00 {

struct FatalMpiFailure {
    int status;
};

inline int& observed_cleanup_count()
{
    static int count = 0;
    return count;
}

inline int throw_abort([[maybe_unused]] MPI_Comm communicator, const int status) { throw FatalMpiFailure{status}; }

inline int report_initialized(int* initialized)
{
    *initialized = 1;
    return MPI_SUCCESS;
}

inline int report_uninitialized(int* initialized)
{
    *initialized = 0;
    return MPI_SUCCESS;
}

inline int report_finalized(int* finalized)
{
    *finalized = 1;
    return MPI_SUCCESS;
}

inline int report_not_finalized(int* finalized)
{
    *finalized = 0;
    return MPI_SUCCESS;
}

inline int fail_lifecycle_query([[maybe_unused]] int* value) { return MPI_ERR_OTHER; }

inline int observe_cleanup(MPI_Comm* communicator)
{
    ++observed_cleanup_count();
    *communicator = MPI_COMM_NULL;
    return MPI_SUCCESS;
}

inline int fail_cleanup([[maybe_unused]] MPI_Comm* communicator) { return MPI_ERR_OTHER; }

inline void expect_fatal_cleanup(const rift::detail::MpiCleanupOperations operations)
{
    using namespace boost::ut;
    MPI_Comm communicator = MPI_COMM_SELF;
    try {
        rift::detail::release_communicator(communicator, operations);
        expect(false);
    }
    catch (const FatalMpiFailure& failure) {
        expect(failure.status == MPI_ERR_OTHER);
    }
}

} // namespace rift_test::run_configuration_cleanup_failures_00

namespace rift_test::run_configuration_cleanup_failures_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "cleanup initialization-query failure enters the fatal path"_test = [] {
        expect_fatal_cleanup({.initialized = fail_lifecycle_query,
                              .finalized = report_not_finalized,
                              .free_communicator = observe_cleanup,
                              .abort = throw_abort});
    };

    "cleanup finalization-query failure enters the fatal path"_test = [] {
        expect_fatal_cleanup({.initialized = report_initialized,
                              .finalized = fail_lifecycle_query,
                              .free_communicator = observe_cleanup,
                              .abort = throw_abort});
    };

    "communicator-free failure enters the fatal path"_test = [] {
        expect_fatal_cleanup({.initialized = report_initialized,
                              .finalized = report_not_finalized,
                              .free_communicator = fail_cleanup,
                              .abort = throw_abort});
    };

    "cleanup skips MPI resources outside the active lifecycle"_test = [] {
        MPI_Comm communicator = MPI_COMM_SELF;
        observed_cleanup_count() = 0;
        rift::detail::release_communicator(communicator, {.initialized = report_uninitialized,
                                                          .finalized = report_not_finalized,
                                                          .free_communicator = observe_cleanup,
                                                          .abort = throw_abort});
        rift::detail::release_communicator(communicator, {.initialized = report_initialized,
                                                          .finalized = report_finalized,
                                                          .free_communicator = observe_cleanup,
                                                          .abort = throw_abort});
        expect(observed_cleanup_count() == 0_i);
    };
}

} // namespace rift_test::run_configuration_cleanup_failures_00
