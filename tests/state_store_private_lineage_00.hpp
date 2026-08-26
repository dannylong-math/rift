#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>

namespace rift_test::state_store_private_lineage_00 {

template<int dim> void check_private_base_root_propagation()
{
    using namespace boost::ut;
    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto store = rift::test::make_state_store(space, rift::StateRetentionPolicy{.max_pinned_private_snapshots = 1});
    const auto accepted0 = store.snapshot(rift::StateSlot::accepted);

    auto parent_trial = store.begin_trial_collective(accepted0.stamp().snapshot).value();
    const auto parent = parent_trial.seal_collective().value();
    auto child_trial = store.begin_trial_collective(parent.stamp().snapshot).value();
    expect(store.pin_collective(parent.stamp().snapshot).has_value());
    const auto child = child_trial.seal_collective().value();

    const auto published_parent = store.publish_collective(parent.stamp().snapshot).value();
    const auto stale_child = store.publish_collective(child.stamp().snapshot);
    expect(!stale_child.has_value());
    if (!stale_child) {
        expect(stale_child.error().code == rift::StateTransitionErrorCode::stale_accepted_root);
    }
    expect(store.snapshot(rift::StateSlot::accepted).stamp().snapshot == published_parent.stamp().snapshot);
    expect(!store.snapshot(child.stamp().snapshot).stamp().published_epoch.has_value());
}

} // namespace rift_test::state_store_private_lineage_00

namespace rift_test::state_store_private_lineage_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "a private-base child retains the accepted root rather than its immediate private base"_test = [] {
        check_private_base_root_propagation<2>();
        check_private_base_root_propagation<3>();
    };
}

} // namespace rift_test::state_store_private_lineage_00
