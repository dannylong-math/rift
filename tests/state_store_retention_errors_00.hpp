#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <cstddef>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <limits>
#include <rift/discrete_state.hpp>
#include <stdexcept>

namespace rift_test::state_store_retention_errors_00 {

template<int dim> void check_retention_errors_and_extreme_capacities()
{
    using namespace boost::ut;
    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto zero = rift::test::make_state_store(space, rift::StateRetentionPolicy{.max_pinned_private_snapshots = 0});
    const auto accepted = zero.snapshot(rift::StateSlot::accepted);
    auto candidate_trial = zero.begin_trial_collective(accepted.stamp().snapshot).value();
    const auto candidate = candidate_trial.seal_collective().value();

    const auto no_capacity = zero.pin_collective(candidate.stamp().snapshot);
    expect(!no_capacity.has_value());
    expect(no_capacity.error().code == rift::StateTransitionErrorCode::pin_limit_reached);
    for (const auto operation : {0, 1}) {
        const auto wrong = operation == 0 ? zero.pin_collective(accepted.stamp().snapshot)
                                          : zero.unpin_collective(accepted.stamp().snapshot);
        expect(!wrong.has_value());
        expect(wrong.error().code == rift::StateTransitionErrorCode::wrong_candidate_state);
    }
    const auto invalid_accepted_discard = zero.discard_collective(accepted.stamp().snapshot);
    expect(!invalid_accepted_discard.has_value());
    expect(invalid_accepted_discard.error().code == rift::StateTransitionErrorCode::invalid_discard);

    const auto unknown = rift::StateSnapshotId::from_index(std::numeric_limits<std::uint64_t>::max());
    const auto unknown_pin = zero.pin_collective(unknown);
    expect(!unknown_pin.has_value());
    expect(unknown_pin.error().code == rift::StateTransitionErrorCode::unknown_snapshot);
    const auto unknown_unpin = zero.unpin_collective(unknown);
    expect(!unknown_unpin.has_value());
    expect(unknown_unpin.error().code == rift::StateTransitionErrorCode::unknown_snapshot);

    const auto published = zero.publish_collective(candidate.stamp().snapshot).value();
    const auto previous = zero.snapshot(rift::StateSlot::previous);
    expect(previous.stamp().snapshot == accepted.stamp().snapshot);
    const auto previous_pin = zero.pin_collective(previous.stamp().snapshot);
    expect(!previous_pin.has_value());
    expect(previous_pin.error().code == rift::StateTransitionErrorCode::wrong_candidate_state);
    const auto previous_unpin = zero.unpin_collective(previous.stamp().snapshot);
    expect(!previous_unpin.has_value());
    expect(previous_unpin.error().code == rift::StateTransitionErrorCode::wrong_candidate_state);
    const auto previous_discard = zero.discard_collective(previous.stamp().snapshot);
    expect(!previous_discard.has_value());
    expect(previous_discard.error().code == rift::StateTransitionErrorCode::invalid_discard);
    expect(published.stamp().published_epoch.has_value());

    auto foreign_store = rift::test::make_state_store(space);
    const auto foreign = foreign_store.snapshot(rift::StateSlot::accepted).stamp().snapshot;
    const auto foreign_pin = zero.pin_collective(foreign);
    expect(!foreign_pin.has_value());
    expect(foreign_pin.error().code == rift::StateTransitionErrorCode::unknown_snapshot);

    auto unlimited = rift::test::make_state_store(
        space, rift::StateRetentionPolicy{.max_pinned_private_snapshots = std::numeric_limits<std::size_t>::max()});
    const auto unlimited_root = unlimited.snapshot(rift::StateSlot::accepted);
    for (int index = 0; index < 4; ++index) {
        auto trial = unlimited.begin_trial_collective(unlimited_root.stamp().snapshot).value();
        const auto pinned = trial.seal_collective().value();
        expect(unlimited.pin_collective(pinned.stamp().snapshot).has_value());
    }

    auto bounded = rift::test::make_state_store(space, rift::StateRetentionPolicy{.max_pinned_private_snapshots = 0});
    const auto bounded_root = bounded.snapshot(rift::StateSlot::accepted);
    auto old = bounded_root.stamp().snapshot;
    for (int index = 0; index < 12; ++index) {
        auto trial = bounded.begin_trial_collective(bounded_root.stamp().snapshot).value();
        const auto newest = trial.seal_collective().value();
        if (index != 0) {
            expect(throws<std::out_of_range>([&] { static_cast<void>(bounded.snapshot(old)); }));
        }
        old = newest.stamp().snapshot;
        expect(bounded.snapshot(old).stamp().snapshot == old);
    }
}

} // namespace rift_test::state_store_retention_errors_00

namespace rift_test::state_store_retention_errors_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "retention errors preserve precedence and extreme capacities do not overflow"_test = [] {
        check_retention_errors_and_extreme_capacities<2>();
        check_retention_errors_and_extreme_capacities<3>();
    };
}

} // namespace rift_test::state_store_retention_errors_00
