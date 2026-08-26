#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <optional>
#include <rift/discrete_state.hpp>

namespace rift_test::state_transition_no_id_consumption_00 {

template<int dim> void check_rejection_does_not_consume_transaction_id()
{
    using namespace boost::ut;
    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto store = rift::test::make_state_store(space);
    const auto base = store.snapshot(rift::StateSlot::accepted).stamp().snapshot;

    auto first = store.begin_trial_collective(base).value();
    const auto rejected = store.begin_trial_collective(rift::StateSnapshotId::from_index(base.value() + 9999U));
    auto second = store.begin_trial_collective(base).value();

    expect(!rejected.has_value());
    expect(rejected.error().code == rift::StateTransitionErrorCode::unknown_snapshot);
    expect(second.id().value() == first.id().value() + 1U);
    expect(store.snapshot(rift::StateSlot::accepted).stamp().snapshot == base);

    first.abandon();
    second.abandon();
    const auto inactive = first.seal_collective();
    expect(!inactive.has_value());
    expect(inactive.error().code == rift::StateTransitionErrorCode::inactive_transaction);

    auto sealed_trial = store.begin_trial_collective(base).value();
    const auto candidate = sealed_trial.seal_collective().value();
    expect(candidate.stamp().snapshot.value() == base.value() + 1U);
    const auto repeated_seal = sealed_trial.seal_collective();
    expect(!repeated_seal.has_value());
    expect(repeated_seal.error().code == rift::StateTransitionErrorCode::inactive_transaction);
    expect(!store.snapshot(candidate.stamp().snapshot).stamp().published_epoch.has_value());

    const auto wrong_publish = store.publish_collective(base);
    const auto wrong_discard = store.discard_collective(base);
    expect(!wrong_publish.has_value());
    expect(wrong_publish.error().code == rift::StateTransitionErrorCode::wrong_candidate_state);
    expect(!wrong_discard.has_value());
    expect(wrong_discard.error().code == rift::StateTransitionErrorCode::invalid_discard);
    expect(store.snapshot(rift::StateSlot::accepted).stamp().snapshot == base);
    expect(!store.snapshot(candidate.stamp().snapshot).stamp().published_epoch.has_value());
    const auto published = store.publish_collective(candidate.stamp().snapshot).value();
    expect(rift::test::require_optional(published.stamp().published_epoch).value() ==
           rift::test::require_optional(store.snapshot(rift::StateSlot::previous).stamp().published_epoch).value() +
               1U);

    auto expiring = std::optional<rift::StateStore>{rift::test::make_state_store(space)};
    const auto expiring_initial = expiring->snapshot(rift::StateSlot::accepted);
    auto expired_trial = expiring->begin_trial_collective(expiring_initial.stamp().snapshot).value();
    expiring.reset();
    const auto expired = expired_trial.seal_collective();
    expect(!expired.has_value());
    expect(expired.error().code == rift::StateTransitionErrorCode::expired_store);
    auto retry_store = rift::test::make_state_store(space);
    expect(retry_store.snapshot(rift::StateSlot::accepted).stamp().snapshot.value() ==
           expiring_initial.stamp().snapshot.value() + 1U);
}

} // namespace rift_test::state_transition_no_id_consumption_00

namespace rift_test::state_transition_no_id_consumption_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "a logical rejection consumes no collective transaction identity in 2D and 3D"_test = [] {
        check_rejection_does_not_consume_transaction_id<2>();
        check_rejection_does_not_consume_transaction_id<3>();
    };
}

} // namespace rift_test::state_transition_no_id_consumption_00
