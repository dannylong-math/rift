#pragma once

#include "../state_result_test_support.hpp"
#include "coverage_profile_flush.hpp"
#include "state_fatal_test_support.hpp"

#include <atomic>
#include <boost/ut.hpp>
#include <cstdio>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <print>
#include <rift/discrete_state.hpp>

namespace rift_test::mpi::state_publish_fatal_00 {
inline int marked_abort(const MPI_Comm communicator, const int status)
{
    rift_test::mpi::flush_coverage_profile();
    const auto* const marker = rift::test::state_target_stage_armed().load(std::memory_order_relaxed)
                                   ? "RIFT_FATAL_ORACLE"
                                   : "RIFT_FATAL_SETUP";
    std::println(stderr, "{} operation=state_publish status={}", marker, status);
    std::fflush(stderr);
    return MPI_Abort(communicator, status);
}

inline int trigger_fatal_operation()
{
    const auto run = rift::test::make_state_fatal_run(marked_abort);
    const auto space = rift::test::make_state_fatal_space<2>(run);
    auto store = rift::test::require_state_result(rift::make_state_store(space.layout(), {}));
    auto transaction = rift::test::require_state_result(
        store.begin_trial_collective(store.snapshot(rift::StateSlot::accepted).stamp().snapshot));
    const auto candidate = rift::test::require_state_result(transaction.seal_collective());
    [[maybe_unused]] const auto result = rift::detail::StateStoreAccess::publish(
        store, candidate.stamp().snapshot, rift::test::fail_state_stage_on_rank_one);
    return 0;
}

inline void register_tests()
{
    using namespace boost::ut;
    "state store publish invokes MPI Abort after a remote staging failure"_test = [] {
        const auto returned = trigger_fatal_operation();
        static_cast<void>(MPI_Barrier(MPI_COMM_WORLD));
        expect(false) << "expected MPI Abort; trigger returned " << returned;
    };
}

} // namespace rift_test::mpi::state_publish_fatal_00
