#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>
#include <stdexcept>

namespace rift_test::state_store_retention_eviction_00 {

template<int dim> void check_immediate_unpin_and_active_base_eviction()
{
    using namespace boost::ut;
    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    const auto field = rift::test::field_reference(space, space.layout().field_blocks().front().id);
    auto store = rift::test::make_state_store(space, rift::StateRetentionPolicy{.max_pinned_private_snapshots = 2});
    const auto accepted = store.snapshot(rift::StateSlot::accepted);

    auto trial_a = store.begin_trial_collective(accepted.stamp().snapshot).value();
    trial_a.field(field).value().get() = 1.0;
    const auto snapshot_a = trial_a.seal_collective().value();
    expect(store.pin_collective(snapshot_a.stamp().snapshot).has_value());

    auto trial_c = store.begin_trial_collective(accepted.stamp().snapshot).value();
    trial_c.field(field).value().get() = 3.0;
    const auto snapshot_c = trial_c.seal_collective().value();
    expect(store.pin_collective(snapshot_c.stamp().snapshot).has_value());

    auto trial_b = store.begin_trial_collective(accepted.stamp().snapshot).value();
    trial_b.field(field).value().get() = 2.0;
    const auto snapshot_b = trial_b.seal_collective().value();
    auto active_from_b_1 = store.begin_trial_collective(snapshot_b.stamp().snapshot).value();
    auto active_from_b_2 = store.begin_trial_collective(snapshot_b.stamp().snapshot).value();

    expect(store.unpin_collective(snapshot_a.stamp().snapshot).has_value());
    expect(throws<std::out_of_range>([&] { static_cast<void>(store.snapshot(snapshot_b.stamp().snapshot)); }));
    expect(store.pin_collective(snapshot_a.stamp().snapshot).has_value());
    const auto pinned_while_b_active = store.discard_collective(snapshot_a.stamp().snapshot);
    expect(!pinned_while_b_active.has_value());
    expect(pinned_while_b_active.error().code == rift::StateTransitionErrorCode::invalid_discard);
    expect(store.unpin_collective(snapshot_a.stamp().snapshot).has_value());
    expect(store.unpin_collective(snapshot_a.stamp().snapshot).has_value());
    expect(store.snapshot(snapshot_a.stamp().snapshot).stamp().snapshot == snapshot_a.stamp().snapshot);

    expect(store.unpin_collective(snapshot_c.stamp().snapshot).has_value());
    expect(throws<std::out_of_range>([&] { static_cast<void>(store.snapshot(snapshot_a.stamp().snapshot)); }));
    expect(store.snapshot(snapshot_c.stamp().snapshot).stamp().snapshot == snapshot_c.stamp().snapshot);

    const auto new_begin = store.begin_trial_collective(snapshot_b.stamp().snapshot);
    expect(!new_begin.has_value());
    expect(new_begin.error().code == rift::StateTransitionErrorCode::unknown_snapshot);
    active_from_b_1.field(field).value().get() = 4.0;
    active_from_b_2.field(field).value().get() = 5.0;
    const auto sealed_1 = active_from_b_1.seal_collective().value();
    expect(sealed_1.field(field).value().get().l2_norm() > 0.0_d);
    const auto sealed_2 = active_from_b_2.seal_collective().value();
    expect(sealed_2.field(field).value().get().l2_norm() > sealed_1.field(field).value().get().l2_norm());
    expect(throws<std::out_of_range>([&] { static_cast<void>(store.snapshot(sealed_1.stamp().snapshot)); }));

    expect(snapshot_a.field(field).value().get().l2_norm() > 0.0_d);
    expect(snapshot_b.field(field).value().get().l2_norm() > snapshot_a.field(field).value().get().l2_norm());
}

} // namespace rift_test::state_store_retention_eviction_00

namespace rift_test::state_store_retention_eviction_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "unpin promotes immediately and active bases survive lookup eviction in 2D and 3D"_test = [] {
        check_immediate_unpin_and_active_base_eviction<2>();
        check_immediate_unpin_and_active_base_eviction<3>();
    };
}

} // namespace rift_test::state_store_retention_eviction_00
