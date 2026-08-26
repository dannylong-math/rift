#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>

namespace rift_test::state_store_retention_publish_00 {

template<int dim> void check_published_pin_and_transient_sibling()
{
    using namespace boost::ut;
    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto store = rift::test::make_state_store(space, rift::StateRetentionPolicy{.max_pinned_private_snapshots = 2});
    const auto accepted = store.snapshot(rift::StateSlot::accepted);

    auto pinned_trial = store.begin_trial_collective(accepted.stamp().snapshot).value();
    const auto pinned = pinned_trial.seal_collective().value();
    expect(store.pin_collective(pinned.stamp().snapshot).has_value());
    auto other_pinned_trial = store.begin_trial_collective(accepted.stamp().snapshot).value();
    const auto other_pinned = other_pinned_trial.seal_collective().value();
    expect(store.pin_collective(other_pinned.stamp().snapshot).has_value());
    auto sibling_trial = store.begin_trial_collective(accepted.stamp().snapshot).value();
    const auto sibling = sibling_trial.seal_collective().value();

    const auto published = store.publish_collective(pinned.stamp().snapshot).value();
    expect(published.stamp().snapshot == pinned.stamp().snapshot);
    expect(store.snapshot(sibling.stamp().snapshot).stamp().snapshot == sibling.stamp().snapshot);
    const auto stale_sibling = store.publish_collective(sibling.stamp().snapshot);
    expect(!stale_sibling.has_value());
    expect(stale_sibling.error().code == rift::StateTransitionErrorCode::stale_accepted_root);
    expect(store.pin_collective(sibling.stamp().snapshot).has_value());

    const auto stale_other_pin = store.publish_collective(other_pinned.stamp().snapshot);
    expect(!stale_other_pin.has_value());
    expect(stale_other_pin.error().code == rift::StateTransitionErrorCode::stale_accepted_root);
    const auto other_pinned_discard = store.discard_collective(other_pinned.stamp().snapshot);
    expect(!other_pinned_discard.has_value());
    expect(other_pinned_discard.error().code == rift::StateTransitionErrorCode::invalid_discard);
}

} // namespace rift_test::state_store_retention_publish_00

namespace rift_test::state_store_retention_publish_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "publishing a pin frees capacity and leaves a transient sibling registered but stale"_test = [] {
        check_published_pin_and_transient_sibling<2>();
        check_published_pin_and_transient_sibling<3>();
    };
}

} // namespace rift_test::state_store_retention_publish_00
