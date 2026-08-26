#pragma once

#include "../src/phase_graph_internal.hpp"
#include "../src/run_configuration_internal.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <memory>
#include <mpi.h>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <new>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <vector>

namespace rift_test::phase_graph_input_allocation_failure_00 {

struct FatalMpiFailure {
    int status;
};

inline int throw_abort([[maybe_unused]] MPI_Comm communicator, const int status) { throw FatalMpiFailure{status}; }

inline rift::detail::CanonicalPhaseGraphInput
fail_input_allocation([[maybe_unused]] const std::vector<rift::PhaseSpecification>& phases,
                      [[maybe_unused]] const std::vector<rift::InterfaceSpecification>& interfaces,
                      [[maybe_unused]] bool callback_available)
{
    throw std::bad_alloc{};
}

} // namespace rift_test::phase_graph_input_allocation_failure_00

namespace rift_test::phase_graph_input_allocation_failure_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "canonical input allocation failure is fatal before exact agreement"_test = [] {
        MPI_Comm duplicate = MPI_COMM_NULL;
        expect(MPI_Comm_dup(MPI_COMM_SELF, &duplicate) == MPI_SUCCESS);
        auto control = std::make_shared<const rift::detail::RunConfigurationControl>(
            duplicate, rift::RunConfigurationId::from_index(99), 0, throw_abort);
        const auto run = rift::detail::RunConfigurationAccess::adopt(control);

        try {
            [[maybe_unused]] const auto unexpected_result = rift::detail::make_phase_graph_with_input_allocator(
                run, {{"phase", "physics"}}, {}, {}, fail_input_allocation);
            expect(false);
        }
        catch (const FatalMpiFailure& failure) {
            expect(failure.status == MPI_ERR_NO_MEM);
        }

        const auto next = rift::make_phase_graph(run, {{"phase", "physics"}}, {});
        expect(next.has_value());
        expect(next->provenance().graph == rift::PhaseGraphInstanceId::from_index(1));
    };
}

} // namespace rift_test::phase_graph_input_allocation_failure_00
