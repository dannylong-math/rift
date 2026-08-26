#pragma once

#include "discrete_state_test_support.hpp"

#include <array>
#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <limits>
#include <rift/discrete_state.hpp>
#include <stdexcept>

namespace rift_test::state_store_retention_exhaustion_00 {

class ScopedSequences {
public:
    explicit ScopedSequences(const std::array<std::uint64_t, 5>& replacement) :
        previous_(rift::detail::StateStoreAccess::replace_identity_sequences_for_test(replacement))
    {
    }

    ScopedSequences(const ScopedSequences&) = delete;
    ScopedSequences& operator=(const ScopedSequences&) = delete;
    ScopedSequences(ScopedSequences&&) = delete;
    ScopedSequences& operator=(ScopedSequences&&) = delete;
    ~ScopedSequences()
    {
        static_cast<void>(rift::detail::StateStoreAccess::replace_identity_sequences_for_test(previous_));
    }

private:
    std::array<std::uint64_t, 5> previous_;
};

template<int dim> void check_exhaustion_preserves_retention_state()
{
    using namespace boost::ut;
    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto store = rift::test::make_state_store(space, rift::StateRetentionPolicy{.max_pinned_private_snapshots = 1});
    const auto accepted = store.snapshot(rift::StateSlot::accepted);
    auto pinned_trial = store.begin_trial_collective(accepted.stamp().snapshot).value();
    const auto pinned = pinned_trial.seal_collective().value();
    expect(store.pin_collective(pinned.stamp().snapshot).has_value());
    auto transient_trial = store.begin_trial_collective(accepted.stamp().snapshot).value();
    const auto transient = transient_trial.seal_collective().value();

    const auto exhausted = static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
    {
        const ScopedSequences restore({0, 0, 0, exhausted, 0});
        const auto failed_publish = store.publish_collective(pinned.stamp().snapshot);
        expect(!failed_publish.has_value());
        expect(failed_publish.error().code == rift::StateTransitionErrorCode::identity_exhausted);
        expect(store.snapshot(rift::StateSlot::accepted).stamp().snapshot == accepted.stamp().snapshot);
        expect(store.snapshot(pinned.stamp().snapshot).stamp().snapshot == pinned.stamp().snapshot);
        const auto still_full = store.pin_collective(transient.stamp().snapshot);
        expect(!still_full.has_value());
        expect(still_full.error().code == rift::StateTransitionErrorCode::pin_limit_reached);
    }
    const auto published = store.publish_collective(pinned.stamp().snapshot).value();
    expect(published.stamp().snapshot == pinned.stamp().snapshot);
    expect(store.pin_collective(transient.stamp().snapshot).has_value());

    auto old_transient_trial = store.begin_trial_collective(published.stamp().snapshot).value();
    const auto old_transient = old_transient_trial.seal_collective().value();
    auto replacement_trial = store.begin_trial_collective(published.stamp().snapshot).value();
    const auto retained_before_failed_seal = old_transient.stamp().snapshot;
    {
        const ScopedSequences restore({0, 0, exhausted, 0, 0});
        const auto failed_seal = replacement_trial.seal_collective();
        expect(!failed_seal.has_value());
        expect(failed_seal.error().code == rift::StateTransitionErrorCode::identity_exhausted);
        expect(replacement_trial.active());
        expect(store.snapshot(retained_before_failed_seal).stamp().snapshot == retained_before_failed_seal);
    }
    const auto replacement = replacement_trial.seal_collective().value();
    expect(store.snapshot(replacement.stamp().snapshot).stamp().snapshot == replacement.stamp().snapshot);
    expect(throws<std::out_of_range>([&] { static_cast<void>(store.snapshot(retained_before_failed_seal)); }));

    auto geometry_store =
        rift::test::make_state_store(space, rift::StateRetentionPolicy{.max_pinned_private_snapshots = 0});
    const auto geometry_accepted = geometry_store.snapshot(rift::StateSlot::accepted);
    auto old_geometry_trial = geometry_store.begin_trial_collective(geometry_accepted.stamp().snapshot).value();
    const auto old_geometry = old_geometry_trial.seal_collective().value();
    auto changed_geometry_trial = geometry_store.begin_trial_collective(geometry_accepted.stamp().snapshot).value();
    const auto level_set = rift::test::field_reference(space, space.level_set_space().id());
    changed_geometry_trial.field(level_set).value().get() = 1.0;
    {
        const ScopedSequences restore({0, 0, 0, 0, exhausted});
        const auto failed_changed_seal = changed_geometry_trial.seal_collective();
        expect(!failed_changed_seal.has_value());
        expect(failed_changed_seal.error().code == rift::StateTransitionErrorCode::identity_exhausted);
        expect(changed_geometry_trial.active());
        expect(geometry_store.snapshot(old_geometry.stamp().snapshot).stamp().snapshot ==
               old_geometry.stamp().snapshot);
    }
    const auto changed_geometry = changed_geometry_trial.seal_collective().value();
    expect(changed_geometry.level_set_snapshot() != geometry_accepted.level_set_snapshot());
    expect(
        throws<std::out_of_range>([&] { static_cast<void>(geometry_store.snapshot(old_geometry.stamp().snapshot)); }));

    auto stale_store =
        rift::test::make_state_store(space, rift::StateRetentionPolicy{.max_pinned_private_snapshots = 1});
    const auto stale_accepted = stale_store.snapshot(rift::StateSlot::accepted);
    auto stale_trial = stale_store.begin_trial_collective(stale_accepted.stamp().snapshot).value();
    const auto stale_candidate = stale_trial.seal_collective().value();
    expect(stale_store.pin_collective(stale_candidate.stamp().snapshot).has_value());
    auto winner_trial = stale_store.begin_trial_collective(stale_accepted.stamp().snapshot).value();
    const auto winner = winner_trial.seal_collective().value();
    static_cast<void>(stale_store.publish_collective(winner.stamp().snapshot).value());
    {
        const ScopedSequences restore({0, 0, 0, exhausted, 0});
        const auto stale_before_exhaustion = stale_store.publish_collective(stale_candidate.stamp().snapshot);
        expect(!stale_before_exhaustion.has_value());
        expect(stale_before_exhaustion.error().code == rift::StateTransitionErrorCode::stale_accepted_root);
        expect(stale_store.snapshot(stale_candidate.stamp().snapshot).stamp().snapshot ==
               stale_candidate.stamp().snapshot);
    }
}

} // namespace rift_test::state_store_retention_exhaustion_00

namespace rift_test::state_store_retention_exhaustion_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "identity exhaustion leaves pin capacity and transient retention atomic"_test = [] {
        check_exhaustion_preserves_retention_state<2>();
        check_exhaustion_preserves_retention_state<3>();
    };
}

} // namespace rift_test::state_store_retention_exhaustion_00
