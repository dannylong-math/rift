#pragma once

#include "../state_result_test_support.hpp"
#include "coverage_profile_flush.hpp"
#include "state_fatal_test_support.hpp"

#include <atomic>
#include <boost/ut.hpp>
#include <cstdio>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <mpi_proto.h>
#include <print>
#include <rift/discrete_state.hpp>

namespace rift_test::mpi::state_regional_sync_fatal_00 {
inline int marked_abort(const MPI_Comm communicator, const int status)
{
    rift_test::mpi::flush_coverage_profile();
    const auto* const marker = rift::test::state_target_stage_armed().load(std::memory_order_relaxed)
                                   ? "RIFT_FATAL_ORACLE"
                                   : "RIFT_FATAL_SETUP";
    std::println(stderr, "{} operation=state_regional_sync status={}", marker, status);
    std::fflush(stderr);
    return MPI_Abort(communicator, status);
}

inline int fail_regional_broadcast(void* buffer, const int count, const MPI_Datatype datatype, const int root,
                                   const MPI_Comm communicator)
{
    int rank = 0;
    if (MPI_Comm_rank(communicator, &rank) != MPI_SUCCESS || rank != 0) {
        rift::test::state_target_stage_armed().store(true, std::memory_order_relaxed);
        return MPI_ERR_OTHER;
    }
    return MPI_Bcast(buffer, count, datatype, root, communicator);
}

inline int trigger_fatal_operation()
{
    const auto run = rift::test::make_state_fatal_run(marked_abort);
    const auto space = rift::test::make_state_fatal_space<2>(run, {{"regional"}});
    auto store = rift::test::require_state_result(rift::make_state_store(space.layout(), {}));
    auto transaction = rift::test::require_state_result(
        store.begin_trial_collective(store.snapshot(rift::StateSlot::accepted).stamp().snapshot));
    [[maybe_unused]] const auto result =
        rift::detail::StateStoreAccess::seal_with_regional_broadcast(transaction, fail_regional_broadcast);
    return 0;
}

inline void register_tests()
{
    using namespace boost::ut;
    "mutable state transaction seal invokes MPI Abort after a regional synchronization failure"_test = [] {
        const auto returned = trigger_fatal_operation();
        static_cast<void>(MPI_Barrier(MPI_COMM_WORLD));
        expect(false) << "expected MPI Abort; trigger returned " << returned;
    };
}

} // namespace rift_test::mpi::state_regional_sync_fatal_00
