#pragma once

#include "../src/run_configuration_internal.hpp"

#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <limits>
#include <mpi.h>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <rift/run_configuration.hpp>

namespace rift_test::run_configuration_mpi_failures_00 {

struct FatalMpiFailure {
    int status;
};

inline int throw_abort([[maybe_unused]] MPI_Comm communicator, const int status) { throw FatalMpiFailure{status}; }

inline int accept_intracommunicator([[maybe_unused]] MPI_Comm communicator, int* is_intercommunicator)
{
    *is_intercommunicator = 0;
    return MPI_SUCCESS;
}

inline int report_intercommunicator([[maybe_unused]] MPI_Comm communicator, int* is_intercommunicator)
{
    *is_intercommunicator = 1;
    return MPI_SUCCESS;
}

inline int fail_query([[maybe_unused]] MPI_Comm communicator, [[maybe_unused]] int* is_intercommunicator)
{
    return MPI_ERR_OTHER;
}

inline int fail_duplicate([[maybe_unused]] MPI_Comm communicator, [[maybe_unused]] MPI_Comm* duplicate)
{
    return MPI_ERR_OTHER;
}

inline int borrow_duplicate(const MPI_Comm communicator, MPI_Comm* duplicate)
{
    *duplicate = communicator;
    return MPI_SUCCESS;
}

inline int& observed_cleanup_count()
{
    static int count = 0;
    return count;
}

inline int observe_communicator_cleanup(MPI_Comm* communicator)
{
    ++observed_cleanup_count();
    *communicator = MPI_COMM_NULL;
    return MPI_SUCCESS;
}

inline int fail_communicator_cleanup([[maybe_unused]] MPI_Comm* communicator) { return MPI_ERR_OTHER; }

inline int fail_broadcast(void* /*value*/, int /*count*/, MPI_Datatype /*datatype*/, int /*root*/,
                          MPI_Comm /*communicator*/)
{
    return MPI_ERR_OTHER;
}

inline int fail_collective_maximum([[maybe_unused]] std::uint64_t local, [[maybe_unused]] std::uint64_t& chosen,
                                   [[maybe_unused]] MPI_Comm communicator)
{
    return MPI_ERR_OTHER;
}

inline int report_undefined_origin(MPI_Group /*source*/, int /*count*/, const int* /*source_ranks*/,
                                   MPI_Group /*destination*/, int* destination_ranks)
{
    *destination_ranks = MPI_UNDEFINED;
    return MPI_SUCCESS;
}

inline int copy_collective_maximum(const std::uint64_t local, std::uint64_t& chosen,
                                   [[maybe_unused]] MPI_Comm communicator)
{
    chosen = local;
    return MPI_SUCCESS;
}

inline rift::detail::MpiRunOperations actual_operations()
{
    return {.test_intercommunicator = MPI_Comm_test_inter,
            .communicator_rank = MPI_Comm_rank,
            .communicator_size = MPI_Comm_size,
            .allocate_rank_buffer = rift::detail::allocate_rank_buffer,
            .communicator_group = MPI_Comm_group,
            .translate_ranks = MPI_Group_translate_ranks,
            .free_group = MPI_Group_free,
            .collective_maximum = copy_collective_maximum,
            .duplicate = MPI_Comm_dup,
            .broadcast = MPI_Bcast,
            .free_communicator = MPI_Comm_free,
            .allocate_control = rift::detail::allocate_run_control,
            .abort = throw_abort};
}

} // namespace rift_test::run_configuration_mpi_failures_00

namespace rift_test::run_configuration_mpi_failures_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "communicator-kind validation accepts an intracommunicator and rejects an intercommunicator"_test = [] {
        expect(
            rift::detail::validate_intracommunicator(MPI_COMM_SELF, accept_intracommunicator, throw_abort).has_value());
        const auto intercommunicator =
            rift::detail::validate_intracommunicator(MPI_COMM_SELF, report_intercommunicator, throw_abort);
        expect(intercommunicator.error().code == rift::RunConfigurationErrorCode::intercommunicator_not_supported);
    };

    "communicator-kind query failure enters the fatal MPI path"_test = [] {
        try {
            expect(rift::detail::validate_intracommunicator(MPI_COMM_SELF, fail_query, throw_abort).has_value());
            expect(false);
        }
        catch (const FatalMpiFailure& failure) {
            expect(failure.status == MPI_ERR_OTHER);
        }
    };

    "communicator duplication failure enters the fatal MPI path"_test = [] {
        try {
            static_cast<void>(rift::detail::duplicate_communicator(MPI_COMM_SELF, fail_duplicate, throw_abort));
            expect(false);
        }
        catch (const FatalMpiFailure& failure) {
            expect(failure.status == MPI_ERR_OTHER);
        }
    };

    "run-ID broadcast failure enters the fatal MPI path"_test = [] {
        rift::detail::RunSequenceAllocator allocator;
        try {
            expect(rift::detail::reserve_run_id(
                       allocator, {.communicator = MPI_COMM_SELF, .communicator_rank = 0, .world_origin = 0},
                       fail_broadcast, throw_abort)
                       .has_value());
            expect(false);
        }
        catch (const FatalMpiFailure& failure) {
            expect(failure.status == MPI_ERR_OTHER);
        }
    };

    "graph-ID collective failure enters the fatal MPI path"_test = [] {
        rift::detail::MonotonicIdAllocator allocator;
        try {
            expect(rift::detail::reserve_collective_id(allocator, MPI_COMM_SELF, fail_collective_maximum, throw_abort)
                       .has_value());
            expect(false);
        }
        catch (const FatalMpiFailure& failure) {
            expect(failure.status == MPI_ERR_OTHER);
        }
    };

    "phase-graph exhaustion retains its domain-specific diagnostic"_test = [] {
        rift::detail::MonotonicIdAllocator allocator(std::numeric_limits<std::uint64_t>::max());
        const auto exhausted =
            rift::detail::reserve_collective_id(allocator, MPI_COMM_SELF, copy_collective_maximum, throw_abort);

        expect(exhausted.error().message == "phase-graph identity space is exhausted");
    };

    "create invokes a fatal handler when duplication fails"_test = [] {
        auto operations = actual_operations();
        operations.duplicate = fail_duplicate;
        rift::detail::RunSequenceAllocator allocator;
        try {
            expect(rift::detail::create_run_configuration(MPI_COMM_SELF, operations, allocator).has_value());
            expect(false);
        }
        catch (const FatalMpiFailure& failure) {
            expect(failure.status == MPI_ERR_OTHER);
        }
    };

    "create rejects a communicator whose origin is outside MPI_COMM_WORLD"_test = [] {
        auto operations = actual_operations();
        operations.translate_ranks = report_undefined_origin;
        rift::detail::RunSequenceAllocator allocator;

        const auto result = rift::detail::create_run_configuration(MPI_COMM_SELF, operations, allocator);

        expect(result.error().code == rift::RunConfigurationErrorCode::communicator_not_world_derived);
    };

    "create releases its duplicate after collective sequence exhaustion"_test = [] {
        auto operations = actual_operations();
        operations.duplicate = borrow_duplicate;
        operations.free_communicator = observe_communicator_cleanup;
        rift::detail::RunSequenceAllocator allocator(std::numeric_limits<std::uint32_t>::max());
        observed_cleanup_count() = 0;

        const auto result = rift::detail::create_run_configuration(MPI_COMM_SELF, operations, allocator);

        expect(result.error().code == rift::RunConfigurationErrorCode::id_space_exhausted);
        expect(observed_cleanup_count() == 1_i);
    };

    "duplicate cleanup failure enters the fatal MPI path"_test = [] {
        auto operations = actual_operations();
        operations.duplicate = borrow_duplicate;
        operations.free_communicator = fail_communicator_cleanup;
        rift::detail::RunSequenceAllocator allocator(std::numeric_limits<std::uint32_t>::max());
        try {
            expect(rift::detail::create_run_configuration(MPI_COMM_SELF, operations, allocator).has_value());
            expect(false);
        }
        catch (const FatalMpiFailure& failure) {
            expect(failure.status == MPI_ERR_OTHER);
        }
    };

    "a non-root run-ID participant accepts the broadcast sequence"_test = [] {
        rift::detail::RunSequenceAllocator allocator(std::numeric_limits<std::uint32_t>::max());
        const auto id = rift::detail::reserve_run_id(
            allocator, {.communicator = MPI_COMM_SELF, .communicator_rank = 1, .world_origin = 7}, MPI_Bcast,
            throw_abort);

        expect(id.error().code == rift::RunConfigurationErrorCode::id_space_exhausted);
    };
}

} // namespace rift_test::run_configuration_mpi_failures_00
