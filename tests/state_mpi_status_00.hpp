#pragma once

#include "mpi/state_fatal_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>

namespace rift_test::state_mpi_status_00 {

struct AbortObserved {
    int status;
};

inline int throwing_abort([[maybe_unused]] MPI_Comm communicator, const int status) { throw AbortObserved{status}; }

inline void check_status_translation()
{
    using namespace boost::ut;

    const auto run = rift::test::make_state_fatal_run(throwing_abort);
    const auto space = rift::test::make_state_fatal_space<2>(run);
    const auto control = rift::detail::StateLayoutAccess::run_control(space.layout());

    rift::detail::require_state_mpi_success(control, {.operation = MPI_SUCCESS, .fatal = MPI_ERR_OTHER});
    try {
        rift::detail::require_state_mpi_success(control, {.operation = MPI_ERR_COUNT, .fatal = MPI_ERR_OTHER});
        expect(false);
    }
    catch (const AbortObserved& observed) {
        expect(observed.status == MPI_ERR_OTHER);
    }
}

} // namespace rift_test::state_mpi_status_00

namespace rift_test::state_mpi_status_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "state MPI status translation is fatal only for non-success"_test = check_status_translation;
}

} // namespace rift_test::state_mpi_status_00
