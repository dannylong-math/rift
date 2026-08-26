#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>

namespace rift_test::state_store_stale_sibling_00 {

template<int dim> void check_stale_sibling()
{
    using namespace boost::ut;
    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto store = rift::test::make_state_store(space, rift::StateRetentionPolicy{.max_pinned_private_snapshots = 1});
    const auto root = store.snapshot(rift::StateSlot::accepted);

    auto first_trial = store.begin_trial_collective(root.stamp().snapshot).value();
    auto second_trial = store.begin_trial_collective(root.stamp().snapshot).value();
    const auto first = first_trial.seal_collective().value();
    expect(store.pin_collective(first.stamp().snapshot).has_value());
    const auto second = second_trial.seal_collective().value();
    const auto published_first = store.publish_collective(first.stamp().snapshot).value();

    const auto rejected = store.publish_collective(second.stamp().snapshot);
    expect(!rejected.has_value());
    expect(rejected.error().code == rift::StateTransitionErrorCode::stale_accepted_root);
    expect(!second.stamp().published_epoch.has_value());
    expect(store.snapshot(rift::StateSlot::accepted).stamp().snapshot == first.stamp().snapshot);
    expect(!store.snapshot(second.stamp().snapshot).stamp().published_epoch.has_value());
    expect(store.discard_collective(second.stamp().snapshot).has_value());

    auto next_trial = store.begin_trial_collective(first.stamp().snapshot).value();
    const auto next_candidate = next_trial.seal_collective().value();
    const auto published_next = store.publish_collective(next_candidate.stamp().snapshot).value();
    expect(rift::test::require_optional(published_next.stamp().published_epoch).value() ==
           rift::test::require_optional(published_first.stamp().published_epoch).value() + 1U);
}

} // namespace rift_test::state_store_stale_sibling_00

namespace rift_test::state_store_stale_sibling_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "a published sibling makes another private sibling stale in 2D and 3D"_test = [] {
        check_stale_sibling<2>();
        check_stale_sibling<3>();
    };
}

} // namespace rift_test::state_store_stale_sibling_00
