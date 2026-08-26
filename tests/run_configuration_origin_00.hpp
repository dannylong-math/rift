#pragma once

#include "../src/run_configuration_internal.hpp"

#include <boost/ut.hpp>
#include <cstddef>
#include <cstdint>
#include <mpi.h>
#include <new>
#include <rift/run_configuration.hpp>
#include <span>
#include <vector>

namespace rift_test::run_configuration_origin_00 {

struct FatalMpiFailure {
    int status;
};

struct ScriptState {
    int failing_call = -1;
    int call_index = 0;
    bool undefined_origin = false;
    bool undefined_non_root = false;
};

inline ScriptState& script_state()
{
    static ScriptState state;
    return state;
}

inline int scripted_status()
{
    auto& state = script_state();
    return state.call_index++ == state.failing_call ? MPI_ERR_OTHER : MPI_SUCCESS;
}

inline int scripted_rank([[maybe_unused]] MPI_Comm communicator, int* rank)
{
    *rank = 0;
    return scripted_status();
}

inline int scripted_size([[maybe_unused]] MPI_Comm communicator, int* size)
{
    *size = script_state().undefined_non_root ? 2 : 1;
    return scripted_status();
}

inline std::vector<int> fail_rank_buffer_allocation([[maybe_unused]] int size) { throw std::bad_alloc{}; }

inline int scripted_group([[maybe_unused]] MPI_Comm communicator, MPI_Group* group)
{
    *group = MPI_GROUP_NULL;
    return scripted_status();
}

inline int scripted_translate(MPI_Group /*source*/, const int count, const int* /*source_ranks*/,
                              MPI_Group /*destination*/, int* destination_ranks)
{
    auto destination = std::span(destination_ranks, static_cast<std::size_t>(count));
    destination.front() = script_state().undefined_origin ? MPI_UNDEFINED : 9;
    if (count > 1) {
        destination.last(static_cast<std::size_t>(count - 1)).front() =
            script_state().undefined_non_root ? MPI_UNDEFINED : 10;
    }
    return scripted_status();
}

inline int scripted_free([[maybe_unused]] MPI_Group* group) { return scripted_status(); }

inline int scripted_collective_maximum(const std::uint64_t local, std::uint64_t& chosen,
                                       [[maybe_unused]] MPI_Comm communicator)
{
    chosen = local;
    return scripted_status();
}

inline int report_unsupported_participant([[maybe_unused]] std::uint64_t local, std::uint64_t& chosen,
                                          [[maybe_unused]] MPI_Comm communicator)
{
    chosen = 1;
    return MPI_SUCCESS;
}

inline int throw_abort([[maybe_unused]] MPI_Comm communicator, const int status) { throw FatalMpiFailure{status}; }

inline rift::detail::MpiRunOperations scripted_operations()
{
    return {.test_intercommunicator = nullptr,
            .communicator_rank = scripted_rank,
            .communicator_size = scripted_size,
            .allocate_rank_buffer = rift::detail::allocate_rank_buffer,
            .communicator_group = scripted_group,
            .translate_ranks = scripted_translate,
            .free_group = scripted_free,
            .collective_maximum = scripted_collective_maximum,
            .duplicate = nullptr,
            .broadcast = nullptr,
            .free_communicator = nullptr,
            .allocate_control = nullptr,
            .abort = throw_abort};
}

} // namespace rift_test::run_configuration_origin_00

namespace rift_test::run_configuration_origin_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "communicator rank zero maps to one MPI world origin"_test = [] {
        script_state() = {.failing_call = -1, .call_index = 0, .undefined_origin = false, .undefined_non_root = false};

        const auto origin = rift::detail::communicator_rank_and_origin(MPI_COMM_SELF, scripted_operations());

        expect(origin.value().first == 0_i);
        expect(origin.value().second == 9_i);
    };

    "a communicator outside MPI_COMM_WORLD has no supported run provenance"_test = [] {
        script_state() = {.failing_call = -1, .call_index = 0, .undefined_origin = true, .undefined_non_root = false};

        const auto origin = rift::detail::communicator_rank_and_origin(MPI_COMM_SELF, scripted_operations());

        expect(origin.error().code == rift::RunConfigurationErrorCode::communicator_not_world_derived);
    };

    "one participant outside MPI_COMM_WORLD rejects the communicator on every rank"_test = [] {
        script_state() = {.failing_call = -1, .call_index = 0, .undefined_origin = false, .undefined_non_root = false};
        auto operations = scripted_operations();
        operations.collective_maximum = report_unsupported_participant;

        const auto origin = rift::detail::communicator_rank_and_origin(MPI_COMM_SELF, operations);

        expect(origin.error().code == rift::RunConfigurationErrorCode::communicator_not_world_derived);
    };

    "a supported root cannot hide an unsupported communicator member"_test = [] {
        script_state() = {.failing_call = -1, .call_index = 0, .undefined_origin = false, .undefined_non_root = true};

        const auto origin = rift::detail::communicator_rank_and_origin(MPI_COMM_SELF, scripted_operations());

        expect(origin.error().code == rift::RunConfigurationErrorCode::communicator_not_world_derived);
    };

    "rank translation allocation failure enters the fatal path"_test = [] {
        script_state() = {.failing_call = -1, .call_index = 0, .undefined_origin = false, .undefined_non_root = false};
        auto operations = scripted_operations();
        operations.allocate_rank_buffer = fail_rank_buffer_allocation;
        try {
            expect(rift::detail::communicator_rank_and_origin(MPI_COMM_SELF, operations).has_value());
            expect(false);
        }
        catch (const FatalMpiFailure& failure) {
            expect(failure.status == MPI_ERR_NO_MEM);
        }
    };

    "every MPI failure while deriving the origin enters the fatal path"_test = [] {
        auto& state = script_state();
        state.undefined_origin = false;
        state.undefined_non_root = false;
        for (state.failing_call = 0; state.failing_call < 8; ++state.failing_call) {
            state.call_index = 0;
            try {
                expect(rift::detail::communicator_rank_and_origin(MPI_COMM_SELF, scripted_operations()).has_value());
                expect(false);
            }
            catch (const FatalMpiFailure& failure) {
                expect(failure.status == MPI_ERR_OTHER);
            }
        }
    };
}

} // namespace rift_test::run_configuration_origin_00
