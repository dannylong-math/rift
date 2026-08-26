#pragma once

#include "../src/run_configuration_internal.hpp"

#include <boost/ut.hpp>
#include <mpi.h>
#include <rift/run_configuration.hpp>

namespace rift_test::run_configuration_lifecycle_failures_00 {

struct FatalMpiFailure {
    int status;
};

inline int throw_abort([[maybe_unused]] MPI_Comm communicator, const int status) { throw FatalMpiFailure{status}; }

inline void expect_fatal_lifecycle(const rift::detail::MpiLifecycleState state)
{
    using namespace boost::ut;
    try {
        expect(rift::detail::validate_mpi_lifecycle(state, throw_abort).has_value());
        expect(false);
    }
    catch (const FatalMpiFailure& failure) {
        expect(failure.status == MPI_ERR_OTHER);
    }
}

} // namespace rift_test::run_configuration_lifecycle_failures_00

namespace rift_test::run_configuration_lifecycle_failures_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "MPI initialization-query failure enters the fatal path"_test = [] {
        expect_fatal_lifecycle({.initialized_status = MPI_ERR_OTHER,
                                .initialized = 1,
                                .finalized_status = MPI_SUCCESS,
                                .finalized = 0,
                                .communicator = MPI_COMM_SELF});
    };

    "MPI finalization-query failure enters the fatal path"_test = [] {
        expect_fatal_lifecycle({.initialized_status = MPI_SUCCESS,
                                .initialized = 1,
                                .finalized_status = MPI_ERR_OTHER,
                                .finalized = 0,
                                .communicator = MPI_COMM_SELF});
    };

    "logical MPI lifecycle values remain recoverable errors"_test = [] {
        const auto uninitialized = rift::detail::validate_mpi_lifecycle({.initialized_status = MPI_SUCCESS,
                                                                         .initialized = 0,
                                                                         .finalized_status = MPI_SUCCESS,
                                                                         .finalized = 0,
                                                                         .communicator = MPI_COMM_SELF},
                                                                        throw_abort);
        expect(uninitialized.error().code == rift::RunConfigurationErrorCode::mpi_not_initialized);

        const auto finalized = rift::detail::validate_mpi_lifecycle({.initialized_status = MPI_SUCCESS,
                                                                     .initialized = 1,
                                                                     .finalized_status = MPI_SUCCESS,
                                                                     .finalized = 1,
                                                                     .communicator = MPI_COMM_SELF},
                                                                    throw_abort);
        expect(finalized.error().code == rift::RunConfigurationErrorCode::mpi_finalized);
    };
}

} // namespace rift_test::run_configuration_lifecycle_failures_00
