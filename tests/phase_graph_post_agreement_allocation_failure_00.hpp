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
#include <optional>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <string>
#include <string_view>

namespace rift_test::phase_graph_post_agreement_allocation_failure_00 {

struct FatalMpiFailure {
    int status;
};

struct UnexpectedRecorderArguments {};

inline int throw_abort([[maybe_unused]] MPI_Comm communicator, const int status) { throw FatalMpiFailure{status}; }

inline void fail_rejection_recording(rift::PhaseGraphErrors& errors,
                                     const rift::InterfaceSpecification& interface_specification,
                                     const rift::PhaseDescriptor& minus_phase, const rift::PhaseDescriptor& plus_phase,
                                     const std::string_view reason)
{
    if (!errors.empty() || interface_specification.name != "first" || minus_phase.name != "a" ||
        plus_phase.name != "b" || reason != "rejected") {
        throw UnexpectedRecorderArguments{};
    }
    throw std::bad_alloc{};
}

} // namespace rift_test::phase_graph_post_agreement_allocation_failure_00

namespace rift_test::phase_graph_post_agreement_allocation_failure_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "allocation failure between compatibility callbacks is fatal"_test = [] {
        MPI_Comm duplicate = MPI_COMM_NULL;
        expect(MPI_Comm_dup(MPI_COMM_SELF, &duplicate) == MPI_SUCCESS);
        auto control = std::make_shared<const rift::detail::RunConfigurationControl>(
            duplicate, rift::RunConfigurationId::from_index(99), 0, throw_abort);
        const auto run = rift::detail::RunConfigurationAccess::adopt(control);
        int callback_calls = 0;
        const rift::InterfaceCompatibilityCheck reject_interface =
            [&callback_calls](const auto&, const auto&, const auto&) -> std::optional<std::string> {
            ++callback_calls;
            return "rejected";
        };

        try {
            [[maybe_unused]] const auto unexpected_result = rift::detail::make_phase_graph_with_input_allocator(
                run, {{"a", "pa"}, {"b", "pb"}, {"c", "pc"}},
                {{"first", "a", "b", "law-1"}, {"second", "b", "c", "law-2"}}, reject_interface,
                rift::detail::allocate_canonical_phase_graph_input, fail_rejection_recording);
            expect(false);
        }
        catch (const FatalMpiFailure& failure) {
            expect(failure.status == MPI_ERR_NO_MEM);
        }

        expect(callback_calls == 1_i);
    };
}

} // namespace rift_test::phase_graph_post_agreement_allocation_failure_00
