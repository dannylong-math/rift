#pragma once

#include "discrete_state_test_support.hpp"

#include <array>
#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <limits>
#include <rift/discrete_state.hpp>

namespace rift_test::state_store_retention_identity_00 {

class SequenceRestorer {
public:
    explicit SequenceRestorer(const std::array<std::uint64_t, 5>& replacement) :
        original_(rift::detail::StateStoreAccess::replace_identity_sequences_for_test(replacement))
    {
    }

    SequenceRestorer(const SequenceRestorer&) = delete;
    SequenceRestorer& operator=(const SequenceRestorer&) = delete;
    SequenceRestorer(SequenceRestorer&&) = delete;
    SequenceRestorer& operator=(SequenceRestorer&&) = delete;
    ~SequenceRestorer()
    {
        static_cast<void>(rift::detail::StateStoreAccess::replace_identity_sequences_for_test(original_));
    }

private:
    std::array<std::uint64_t, 5> original_;
};

inline void expect_sequences_unchanged(const std::array<std::uint64_t, 5>& expected)
{
    using namespace boost::ut;
    const auto observed = rift::detail::StateStoreAccess::replace_identity_sequences_for_test(expected);
    expect(observed == expected);
}

template<int dim> void check_pin_unpin_discard_reserve_no_ids()
{
    using namespace boost::ut;
    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto store = rift::test::make_state_store(space, rift::StateRetentionPolicy{.max_pinned_private_snapshots = 1});
    const auto accepted = store.snapshot(rift::StateSlot::accepted);
    auto first_trial = store.begin_trial_collective(accepted.stamp().snapshot).value();
    const auto first = first_trial.seal_collective().value();
    expect(store.pin_collective(first.stamp().snapshot).has_value());
    auto second_trial = store.begin_trial_collective(accepted.stamp().snapshot).value();
    const auto second = second_trial.seal_collective().value();

    auto fresh_store =
        rift::test::make_state_store(space, rift::StateRetentionPolicy{.max_pinned_private_snapshots = 1});
    const auto fresh_accepted = fresh_store.snapshot(rift::StateSlot::accepted);
    auto fresh_trial = fresh_store.begin_trial_collective(fresh_accepted.stamp().snapshot).value();
    const auto fresh_candidate = fresh_trial.seal_collective().value();

    const auto exhausted = static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
    const std::array<std::uint64_t, 5> sentinel{exhausted, exhausted, exhausted, exhausted, exhausted};
    const SequenceRestorer restore(sentinel);

    expect(fresh_store.pin_collective(fresh_candidate.stamp().snapshot).has_value());
    expect_sequences_unchanged(sentinel);
    const auto fresh_pinned_discard = fresh_store.discard_collective(fresh_candidate.stamp().snapshot);
    expect(!fresh_pinned_discard.has_value());
    expect(fresh_pinned_discard.error().code == rift::StateTransitionErrorCode::invalid_discard);
    expect_sequences_unchanged(sentinel);

    const auto failed_pin = store.pin_collective(second.stamp().snapshot);
    expect(!failed_pin.has_value());
    expect(failed_pin.error().code == rift::StateTransitionErrorCode::pin_limit_reached);
    expect_sequences_unchanged(sentinel);
    expect(store.pin_collective(first.stamp().snapshot).has_value());
    expect_sequences_unchanged(sentinel);
    expect(store.unpin_collective(first.stamp().snapshot).has_value());
    expect_sequences_unchanged(sentinel);
    expect(store.unpin_collective(first.stamp().snapshot).has_value());
    expect_sequences_unchanged(sentinel);
    expect(store.discard_collective(first.stamp().snapshot).has_value());
    expect_sequences_unchanged(sentinel);
    const auto wrong = store.pin_collective(accepted.stamp().snapshot);
    expect(!wrong.has_value());
    expect_sequences_unchanged(sentinel);
    const auto unknown = store.discard_collective(second.stamp().snapshot);
    expect(!unknown.has_value());
    expect_sequences_unchanged(sentinel);
}

} // namespace rift_test::state_store_retention_identity_00

namespace rift_test::state_store_retention_identity_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "pin unpin and discard reserve no identity component on success or failure"_test = [] {
        check_pin_unpin_discard_reserve_no_ids<2>();
        check_pin_unpin_discard_reserve_no_ids<3>();
    };
}

} // namespace rift_test::state_store_retention_identity_00
