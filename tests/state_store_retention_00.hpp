#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>
#include <stdexcept>

namespace rift_test::state_store_retention_00 {

template<int dim> void check_pin_capacity_and_transient_retention()
{
    using namespace boost::ut;
    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto store =
        rift::make_state_store(space.layout(), rift::StateRetentionPolicy{.max_pinned_private_snapshots = 1}).value();
    const auto accepted = store.snapshot(rift::StateSlot::accepted);

    auto pinned_trial = store.begin_trial_collective(accepted.stamp().snapshot).value();
    const auto pinned = pinned_trial.seal_collective().value();
    expect(store.pin_collective(pinned.stamp().snapshot).has_value());
    expect(store.pin_collective(pinned.stamp().snapshot).has_value());

    auto transient_trial = store.begin_trial_collective(accepted.stamp().snapshot).value();
    const auto transient = transient_trial.seal_collective().value();
    const auto at_limit = store.pin_collective(transient.stamp().snapshot);
    expect(!at_limit.has_value());
    expect(at_limit.error().code == rift::StateTransitionErrorCode::pin_limit_reached);
    expect(store.snapshot(transient.stamp().snapshot).stamp().snapshot == transient.stamp().snapshot);

    const auto pinned_discard = store.discard_collective(pinned.stamp().snapshot);
    expect(!pinned_discard.has_value());
    expect(pinned_discard.error().code == rift::StateTransitionErrorCode::invalid_discard);
    expect(store.discard_collective(transient.stamp().snapshot).has_value());
    expect(throws<std::out_of_range>([&] { static_cast<void>(store.snapshot(transient.stamp().snapshot)); }));

    expect(store.unpin_collective(pinned.stamp().snapshot).has_value());
    expect(store.unpin_collective(pinned.stamp().snapshot).has_value());
    expect(store.pin_collective(pinned.stamp().snapshot).has_value());
}

} // namespace rift_test::state_store_retention_00

namespace rift_test::state_store_retention_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "pinning is bounded, idempotent, and protects private snapshots in 2D and 3D"_test = [] {
        check_pin_capacity_and_transient_retention<2>();
        check_pin_capacity_and_transient_retention<3>();
    };
}

} // namespace rift_test::state_store_retention_00
