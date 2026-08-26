#pragma once

#include "../src/run_configuration_internal.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <memory>
#include <mpi.h>
#include <mpi_proto.h>
#include <new>
#include <rift/discrete_state.hpp>
#include <rift/run_configuration.hpp>
#include <stdexcept>

namespace rift_test::space_registry_fatal_00 {

struct FatalStatus {
    int value;
};

inline int throw_fatal([[maybe_unused]] MPI_Comm communicator, const int status) { throw FatalStatus{status}; }

} // namespace rift_test::space_registry_fatal_00

namespace rift_test::space_registry_fatal_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "unsafe space failures use the fatal handler retained by the run"_test = [] {
        MPI_Comm owned = MPI_COMM_NULL;
        expect(MPI_Comm_dup(MPI_COMM_SELF, &owned) == MPI_SUCCESS);
        auto control = std::make_shared<const rift::detail::RunConfigurationControl>(
            owned, rift::RunConfigurationId::from_index(0), 0, throw_fatal);
        bool observed = false;
        try {
            rift::detail::abort_space_operation(control, MPI_ERR_NO_MEM);
        }
        catch (const FatalStatus& failure) {
            observed = true;
            expect(failure.value == MPI_ERR_NO_MEM);
        }
        expect(observed);

        const auto expect_boundary_status = [&](const int expected_status, auto&& operation) {
            bool boundary_observed = false;
            try {
                static_cast<void>(rift::detail::invoke_space_fatal_boundary(control, operation));
            }
            catch (const FatalStatus& failure) {
                boundary_observed = true;
                expect(failure.value == expected_status);
            }
            expect(boundary_observed);
        };
        expect_boundary_status(MPI_ERR_NO_MEM, []() -> int { throw std::bad_alloc{}; });
        expect_boundary_status(MPI_ERR_OTHER, []() -> int { throw std::runtime_error("dependency failure"); });

        unsigned int reads = 0;
        rift::detail::verify_synchronized_active_fe_index(control, true, 0, [&] {
            ++reads;
            return 1U;
        });
        expect(reads == 0_u);
        rift::detail::verify_synchronized_active_fe_index(control, false, 0, [&] {
            ++reads;
            return 0U;
        });
        expect(reads == 1_u);
        expect_boundary_status(MPI_ERR_OTHER, [&] {
            rift::detail::verify_synchronized_active_fe_index(control, false, 0, [] { return 1U; });
            return 0;
        });
    };
}

} // namespace rift_test::space_registry_fatal_00
