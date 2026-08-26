#pragma once

#include "../src/run_configuration_internal.hpp"

#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <memory>
#include <mpi.h>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <rift/run_configuration.hpp>

namespace rift_test::run_configuration_allocation_failures_00 {

struct FatalMpiFailure {
    int status;
};

inline int& observed_cleanup_count()
{
    static int count = 0;
    return count;
}

inline int throw_abort([[maybe_unused]] MPI_Comm communicator, const int status) { throw FatalMpiFailure{status}; }

inline int borrow_duplicate(const MPI_Comm communicator, MPI_Comm* duplicate)
{
    *duplicate = communicator;
    return MPI_SUCCESS;
}

inline int observe_cleanup(MPI_Comm* communicator)
{
    ++observed_cleanup_count();
    *communicator = MPI_COMM_NULL;
    return MPI_SUCCESS;
}

inline int copy_collective_maximum(const std::uint64_t local, std::uint64_t& chosen,
                                   [[maybe_unused]] MPI_Comm communicator)
{
    chosen = local;
    return MPI_SUCCESS;
}

inline std::shared_ptr<const rift::detail::RunConfigurationControl>
fail_control_allocation([[maybe_unused]] MPI_Comm communicator, [[maybe_unused]] rift::RunConfigurationId id,
                        [[maybe_unused]] rift::detail::MpiAbort abort)
{
    throw std::bad_alloc{};
}

inline rift::detail::MpiRunOperations allocation_failure_operations()
{
    return {.test_intercommunicator = MPI_Comm_test_inter,
            .communicator_rank = MPI_Comm_rank,
            .communicator_size = MPI_Comm_size,
            .allocate_rank_buffer = rift::detail::allocate_rank_buffer,
            .communicator_group = MPI_Comm_group,
            .translate_ranks = MPI_Group_translate_ranks,
            .free_group = MPI_Group_free,
            .collective_maximum = copy_collective_maximum,
            .duplicate = borrow_duplicate,
            .broadcast = MPI_Bcast,
            .free_communicator = observe_cleanup,
            .allocate_control = fail_control_allocation,
            .abort = throw_abort};
}

} // namespace rift_test::run_configuration_allocation_failures_00

namespace rift_test::run_configuration_allocation_failures_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "control allocation failure enters the fatal path without a collective cleanup"_test = [] {
        auto operations = allocation_failure_operations();
        rift::detail::RunSequenceAllocator allocator;
        observed_cleanup_count() = 0;
        try {
            expect(rift::detail::create_run_configuration(MPI_COMM_SELF, operations, allocator).has_value());
            expect(false);
        }
        catch (const FatalMpiFailure& failure) {
            expect(failure.status == MPI_ERR_NO_MEM);
            expect(observed_cleanup_count() == 0_i);
        }
    };
}

} // namespace rift_test::run_configuration_allocation_failures_00
