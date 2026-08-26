#pragma once

#include "../state_result_test_support.hpp"
#include "coverage_profile_flush.hpp"
#include "state_fatal_test_support.hpp"

#include <atomic>
#include <boost/ut.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <mpi_proto.h>
#include <new>
#include <print>
#include <rift/discrete_state.hpp>
#include <vector>

namespace rift_test::mpi::state_regional_staging_fatal_00 {
inline int marked_abort(const MPI_Comm communicator, const int status)
{
    rift_test::mpi::flush_coverage_profile();
    const auto* const marker = rift::test::state_target_stage_armed().load(std::memory_order_relaxed)
                                   ? "RIFT_FATAL_ORACLE"
                                   : "RIFT_FATAL_SETUP";
    std::println(stderr, "{} operation=state_regional_staging status={}", marker, status);
    std::fflush(stderr);
    return MPI_Abort(communicator, status);
}

inline std::vector<std::uint64_t> fail_nonroot_staging(const std::size_t size)
{
    int rank = 0;
    if (MPI_Comm_rank(MPI_COMM_WORLD, &rank) != MPI_SUCCESS || rank != 0) {
        rift::test::state_target_stage_armed().store(true, std::memory_order_relaxed);
        throw std::bad_alloc{};
    }
    return std::vector<std::uint64_t>(size);
}

inline int trigger_fatal_operation()
{
    const auto run = rift::test::make_state_fatal_run(marked_abort);
    const auto space = rift::test::make_state_fatal_space<2>(run, {{"regional"}});
    auto store = rift::test::require_state_result(rift::make_state_store(space.layout(), {}));
    auto transaction = rift::test::require_state_result(
        store.begin_trial_collective(store.snapshot(rift::StateSlot::accepted).stamp().snapshot));
    [[maybe_unused]] const auto result =
        rift::detail::StateStoreAccess::seal_with_regional_staging(transaction, fail_nonroot_staging);
    return 0;
}

inline void register_tests()
{
    using namespace boost::ut;
    "mutable state transaction seal invokes MPI Abort after a regional staging failure"_test = [] {
        const auto returned = trigger_fatal_operation();
        static_cast<void>(MPI_Barrier(MPI_COMM_WORLD));
        expect(false) << "expected MPI Abort; trigger returned " << returned;
    };
}

} // namespace rift_test::mpi::state_regional_staging_fatal_00
