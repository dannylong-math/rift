#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>
#include <stdexcept>
#include <tuple>

namespace rift_test::state_store_retention_lifetime_00 {

template<int dim> void check_move_and_external_lifetime()
{
    using namespace boost::ut;
    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    const auto field = rift::test::field_reference(space, space.layout().field_blocks().front().id);
    const rift::StateSnapshot pinned_handle = [&] {
        auto [moved, pinned, transient] = [&] {
            auto original =
                rift::test::make_state_store(space, rift::StateRetentionPolicy{.max_pinned_private_snapshots = 1});
            const auto accepted = original.snapshot(rift::StateSlot::accepted);
            auto pin_trial = original.begin_trial_collective(accepted.stamp().snapshot).value();
            pin_trial.field(field).value().get() = 7.0;
            auto pinned_candidate = pin_trial.seal_collective().value();
            expect(original.pin_collective(pinned_candidate.stamp().snapshot).has_value());
            auto transient_trial = original.begin_trial_collective(accepted.stamp().snapshot).value();
            transient_trial.field(field).value().get() = 8.0;
            auto transient_candidate = transient_trial.seal_collective().value();
            return std::tuple{std::move(original), std::move(pinned_candidate), std::move(transient_candidate)};
        }();
        expect(moved.snapshot(pinned.stamp().snapshot).stamp().snapshot == pinned.stamp().snapshot);
        expect(moved.snapshot(transient.stamp().snapshot).stamp().snapshot == transient.stamp().snapshot);
        const auto published = moved.publish_collective(pinned.stamp().snapshot).value();
        expect(published.stamp().snapshot == pinned.stamp().snapshot);
        expect(moved.pin_collective(transient.stamp().snapshot).has_value());
        expect(moved.unpin_collective(transient.stamp().snapshot).has_value());
        return pinned;
    }();
    expect(pinned_handle.field(field).value().get().l2_norm() > 0.0_d);

    const rift::StateSnapshot evicted_handle = [&] {
        auto store = rift::test::make_state_store(space, rift::StateRetentionPolicy{.max_pinned_private_snapshots = 0});
        const auto accepted = store.snapshot(rift::StateSlot::accepted);
        auto first_trial = store.begin_trial_collective(accepted.stamp().snapshot).value();
        first_trial.field(field).value().get() = 9.0;
        auto first = first_trial.seal_collective().value();
        auto replacement_trial = store.begin_trial_collective(accepted.stamp().snapshot).value();
        const auto replacement = replacement_trial.seal_collective().value();
        expect(throws<std::out_of_range>([&] { static_cast<void>(store.snapshot(first.stamp().snapshot)); }));
        expect(store.discard_collective(replacement.stamp().snapshot).has_value());
        return first;
    }();
    expect(evicted_handle.field(field).value().get().l2_norm() > pinned_handle.field(field).value().get().l2_norm());
}

} // namespace rift_test::state_store_retention_lifetime_00

namespace rift_test::state_store_retention_lifetime_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "store moves preserve retention and external handles outlive every removal"_test = [] {
        check_move_and_external_lifetime<2>();
        check_move_and_external_lifetime<3>();
    };
}

} // namespace rift_test::state_store_retention_lifetime_00
