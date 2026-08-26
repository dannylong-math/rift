#pragma once

#include "../state_result_test_support.hpp"
#include "state_collective_test_support.hpp"

#include <algorithm>
#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <optional>
#include <rift/discrete_state.hpp>
#include <stdexcept>

namespace rift_test::mpi::state_retention_agreement_00 {

inline void expect_error(const rift::StateTransitionResult<void>& result, const rift::StateTransitionErrorCode code)
{
    using namespace boost::ut;
    expect(!result.has_value());
    expect(result.error().code == code);
}

inline void expect_collectively_known(rift::StateStore& store, const rift::StateSnapshotId id)
{
    using namespace boost::ut;
    int locally_known = 1;
    try {
        static_cast<void>(store.snapshot(id));
    }
    catch (const std::out_of_range&) {
        locally_known = 0;
    }
    const auto known_by_rank = dealii::Utilities::MPI::all_gather(MPI_COMM_WORLD, locally_known);
    expect(std::ranges::all_of(known_by_rank, [](const auto value) { return value == 1; }));
}

template<int dim> void check_collective_retention_agreement()
{
    using namespace boost::ut;
    const auto rank = dealii::Utilities::MPI::this_mpi_process(MPI_COMM_WORLD);
    const auto size = dealii::Utilities::MPI::n_mpi_processes(MPI_COMM_WORLD);
    const auto outlier = size == 3U ? 1U : size - 1U;
    const auto fixture = rift::test::make_distributed_state_fixture<dim>();
    const auto policy = rift::StateRetentionPolicy{.max_pinned_private_snapshots = 1};

    auto argument_store = rift::test::require_state_result(
        rift::make_state_store(fixture.space.layout(), rift::StateRetentionPolicy{.max_pinned_private_snapshots = 2}));
    const auto argument_accepted = argument_store.snapshot(rift::StateSlot::accepted);
    auto argument_a_trial =
        rift::test::require_state_result(argument_store.begin_trial_collective(argument_accepted.stamp().snapshot));
    const auto argument_a = rift::test::require_state_result(argument_a_trial.seal_collective());
    expect(argument_store.pin_collective(argument_a.stamp().snapshot).has_value());
    auto argument_b_trial =
        rift::test::require_state_result(argument_store.begin_trial_collective(argument_accepted.stamp().snapshot));
    auto argument_b = rift::test::require_state_result(argument_b_trial.seal_collective());

    expect_error(
        argument_store.pin_collective(rank == outlier ? argument_b.stamp().snapshot : argument_a.stamp().snapshot),
        rift::StateTransitionErrorCode::argument_mismatch);
    expect(argument_store.discard_collective(argument_b.stamp().snapshot).has_value());
    expect_error(argument_store.discard_collective(argument_a.stamp().snapshot),
                 rift::StateTransitionErrorCode::invalid_discard);

    argument_b_trial =
        rift::test::require_state_result(argument_store.begin_trial_collective(argument_accepted.stamp().snapshot));
    argument_b = rift::test::require_state_result(argument_b_trial.seal_collective());
    expect_error(
        argument_store.unpin_collective(rank == outlier ? argument_b.stamp().snapshot : argument_a.stamp().snapshot),
        rift::StateTransitionErrorCode::argument_mismatch);
    expect(argument_store.discard_collective(argument_b.stamp().snapshot).has_value());
    expect_error(argument_store.discard_collective(argument_a.stamp().snapshot),
                 rift::StateTransitionErrorCode::invalid_discard);

    argument_b_trial =
        rift::test::require_state_result(argument_store.begin_trial_collective(argument_accepted.stamp().snapshot));
    argument_b = rift::test::require_state_result(argument_b_trial.seal_collective());
    expect_error(
        argument_store.discard_collective(rank == outlier ? argument_a.stamp().snapshot : argument_b.stamp().snapshot),
        rift::StateTransitionErrorCode::argument_mismatch);
    expect_collectively_known(argument_store, argument_b.stamp().snapshot);
    expect(argument_store.discard_collective(argument_b.stamp().snapshot).has_value());

    auto store = rift::test::require_state_result(rift::make_state_store(fixture.space.layout(), policy));
    const auto accepted = store.snapshot(rift::StateSlot::accepted);

    auto trial_a = rift::test::require_state_result(store.begin_trial_collective(accepted.stamp().snapshot));
    const auto snapshot_a = rift::test::require_state_result(trial_a.seal_collective());
    expect(store.pin_collective(snapshot_a.stamp().snapshot).has_value());
    auto trial_b = rift::test::require_state_result(store.begin_trial_collective(accepted.stamp().snapshot));
    const auto snapshot_b = rift::test::require_state_result(trial_b.seal_collective());

    const auto pin_unpin = rank == outlier ? store.unpin_collective(snapshot_a.stamp().snapshot)
                                           : store.pin_collective(snapshot_a.stamp().snapshot);
    expect_error(pin_unpin, rift::StateTransitionErrorCode::operation_mismatch);
    const auto pin_discard = rank == outlier ? store.discard_collective(snapshot_b.stamp().snapshot)
                                             : store.pin_collective(snapshot_b.stamp().snapshot);
    expect_error(pin_discard, rift::StateTransitionErrorCode::operation_mismatch);
    const auto unpin_discard = rank == outlier ? store.discard_collective(snapshot_b.stamp().snapshot)
                                               : store.unpin_collective(snapshot_a.stamp().snapshot);
    expect_error(unpin_discard, rift::StateTransitionErrorCode::operation_mismatch);

    {
        auto primary = rift::test::require_state_result(rift::make_state_store(fixture.space.layout(), policy));
        auto alternate = rift::test::require_state_result(rift::make_state_store(fixture.space.layout(), policy));
        const auto primary_accepted = primary.snapshot(rift::StateSlot::accepted);
        const auto alternate_accepted = alternate.snapshot(rift::StateSlot::accepted);
        auto primary_trial =
            rift::test::require_state_result(primary.begin_trial_collective(primary_accepted.stamp().snapshot));
        auto alternate_trial =
            rift::test::require_state_result(alternate.begin_trial_collective(alternate_accepted.stamp().snapshot));
        const auto primary_candidate = rift::test::require_state_result(primary_trial.seal_collective());
        const auto alternate_candidate = rift::test::require_state_result(alternate_trial.seal_collective());
        auto& selected = rank == outlier ? alternate : primary;
        const auto selected_id =
            rank == outlier ? alternate_candidate.stamp().snapshot : primary_candidate.stamp().snapshot;
        expect_error(selected.pin_collective(selected_id), rift::StateTransitionErrorCode::store_mismatch);
        expect(primary.discard_collective(primary_candidate.stamp().snapshot).has_value());
        expect(alternate.discard_collective(alternate_candidate.stamp().snapshot).has_value());
    }
    {
        auto primary = rift::test::require_state_result(rift::make_state_store(fixture.space.layout(), policy));
        auto alternate = rift::test::require_state_result(rift::make_state_store(fixture.space.layout(), policy));
        const auto primary_accepted = primary.snapshot(rift::StateSlot::accepted);
        const auto alternate_accepted = alternate.snapshot(rift::StateSlot::accepted);
        auto primary_pinned_trial =
            rift::test::require_state_result(primary.begin_trial_collective(primary_accepted.stamp().snapshot));
        auto alternate_pinned_trial =
            rift::test::require_state_result(alternate.begin_trial_collective(alternate_accepted.stamp().snapshot));
        const auto primary_pinned = rift::test::require_state_result(primary_pinned_trial.seal_collective());
        const auto alternate_pinned = rift::test::require_state_result(alternate_pinned_trial.seal_collective());
        expect(primary.pin_collective(primary_pinned.stamp().snapshot).has_value());
        expect(alternate.pin_collective(alternate_pinned.stamp().snapshot).has_value());
        auto primary_transient_trial =
            rift::test::require_state_result(primary.begin_trial_collective(primary_accepted.stamp().snapshot));
        auto alternate_transient_trial =
            rift::test::require_state_result(alternate.begin_trial_collective(alternate_accepted.stamp().snapshot));
        const auto primary_transient = rift::test::require_state_result(primary_transient_trial.seal_collective());
        const auto alternate_transient = rift::test::require_state_result(alternate_transient_trial.seal_collective());
        auto& selected = rank == outlier ? alternate : primary;
        const auto selected_id = rank == outlier ? alternate_pinned.stamp().snapshot : primary_pinned.stamp().snapshot;
        expect_error(selected.unpin_collective(selected_id), rift::StateTransitionErrorCode::store_mismatch);
        expect_collectively_known(primary, primary_transient.stamp().snapshot);
        expect_collectively_known(alternate, alternate_transient.stamp().snapshot);
        expect_error(primary.pin_collective(primary_transient.stamp().snapshot),
                     rift::StateTransitionErrorCode::pin_limit_reached);
        expect_error(alternate.pin_collective(alternate_transient.stamp().snapshot),
                     rift::StateTransitionErrorCode::pin_limit_reached);
    }
    {
        auto primary = rift::test::require_state_result(rift::make_state_store(fixture.space.layout(), policy));
        auto alternate = rift::test::require_state_result(rift::make_state_store(fixture.space.layout(), policy));
        const auto primary_accepted = primary.snapshot(rift::StateSlot::accepted);
        const auto alternate_accepted = alternate.snapshot(rift::StateSlot::accepted);
        auto primary_trial =
            rift::test::require_state_result(primary.begin_trial_collective(primary_accepted.stamp().snapshot));
        auto alternate_trial =
            rift::test::require_state_result(alternate.begin_trial_collective(alternate_accepted.stamp().snapshot));
        const auto primary_candidate = rift::test::require_state_result(primary_trial.seal_collective());
        const auto alternate_candidate = rift::test::require_state_result(alternate_trial.seal_collective());
        auto& selected = rank == outlier ? alternate : primary;
        const auto selected_id =
            rank == outlier ? alternate_candidate.stamp().snapshot : primary_candidate.stamp().snapshot;
        expect_error(selected.discard_collective(selected_id), rift::StateTransitionErrorCode::store_mismatch);
        expect_collectively_known(primary, primary_candidate.stamp().snapshot);
        expect_collectively_known(alternate, alternate_candidate.stamp().snapshot);
        expect(primary.discard_collective(primary_candidate.stamp().snapshot).has_value());
        expect(alternate.discard_collective(alternate_candidate.stamp().snapshot).has_value());
    }

    const auto unknown = rift::StateSnapshotId::from_index(snapshot_b.stamp().snapshot.value() + 100000U);
    expect_error(store.pin_collective(unknown), rift::StateTransitionErrorCode::unknown_snapshot);
    expect_error(store.pin_collective(accepted.stamp().snapshot),
                 rift::StateTransitionErrorCode::wrong_candidate_state);
    expect_error(store.discard_collective(snapshot_a.stamp().snapshot),
                 rift::StateTransitionErrorCode::invalid_discard);

    auto before = rift::test::require_state_result(store.begin_trial_collective(accepted.stamp().snapshot));
    expect_error(store.pin_collective(snapshot_b.stamp().snapshot), rift::StateTransitionErrorCode::pin_limit_reached);
    auto after = rift::test::require_state_result(store.begin_trial_collective(accepted.stamp().snapshot));
    expect(after.id().value() == before.id().value() + 1U);
    before.abandon();
    after.abandon();
    expect(store.snapshot(snapshot_a.stamp().snapshot).stamp().snapshot == snapshot_a.stamp().snapshot);
    expect(store.snapshot(snapshot_b.stamp().snapshot).stamp().snapshot == snapshot_b.stamp().snapshot);

    const auto published_a = rift::test::require_state_result(store.publish_collective(snapshot_a.stamp().snapshot));
    expect(store.snapshot(snapshot_b.stamp().snapshot).stamp().snapshot == snapshot_b.stamp().snapshot);
    const auto stale_b = store.publish_collective(snapshot_b.stamp().snapshot);
    expect(!stale_b.has_value());
    expect(stale_b.error().code == rift::StateTransitionErrorCode::stale_accepted_root);
    expect(store.pin_collective(snapshot_b.stamp().snapshot).has_value());

    const auto field = rift::test::flow_reference(fixture.space);
    std::optional<rift::StateSnapshot> outlier_external;
    rift::StateSnapshotId transient_id = snapshot_b.stamp().snapshot;
    {
        auto transient_trial =
            rift::test::require_state_result(store.begin_trial_collective(published_a.stamp().snapshot));
        auto& values = rift::test::require_state_result(transient_trial.field(field)).get();
        if (rank == outlier) {
            values = 19.0;
        }
        const auto transient = rift::test::require_state_result(transient_trial.seal_collective());
        transient_id = transient.stamp().snapshot;
        if (rank == outlier) {
            outlier_external.emplace(transient);
        }
    }
    expect(store.unpin_collective(snapshot_b.stamp().snapshot).has_value());
    int locally_unknown = 0;
    try {
        static_cast<void>(store.snapshot(transient_id));
    }
    catch (const std::out_of_range&) {
        locally_unknown = 1;
    }
    const auto unknown_by_rank = dealii::Utilities::MPI::all_gather(MPI_COMM_WORLD, locally_unknown);
    expect(std::ranges::all_of(unknown_by_rank, [](const auto value) { return value == 1; }));
    if (rank == outlier) {
        if (!outlier_external) {
            throw std::logic_error("outlier did not retain its external snapshot handle");
        }
        const auto& retained_values = rift::test::require_state_result(outlier_external->field(field)).get();
        expect(retained_values.locally_owned_size() > 0U);
        expect(retained_values.local_element(0) == 19.0_d);
    }

    auto base_trial = rift::test::require_state_result(store.begin_trial_collective(snapshot_b.stamp().snapshot));
    const auto base = rift::test::require_state_result(base_trial.seal_collective());
    auto active_one = rift::test::require_state_result(store.begin_trial_collective(base.stamp().snapshot));
    auto active_two = rift::test::require_state_result(store.begin_trial_collective(base.stamp().snapshot));
    auto eviction_trial = rift::test::require_state_result(store.begin_trial_collective(published_a.stamp().snapshot));
    static_cast<void>(rift::test::require_state_result(eviction_trial.seal_collective()));
    const auto evicted_begin = store.begin_trial_collective(base.stamp().snapshot);
    expect(!evicted_begin.has_value());
    expect(evicted_begin.error().code == rift::StateTransitionErrorCode::unknown_snapshot);
    expect(active_one.seal_collective().has_value());
    expect(active_two.seal_collective().has_value());
}

inline void register_tests()
{
    using namespace boost::ut;
    "new retention operations agree before mutation across two and three ranks"_test = [] {
        check_collective_retention_agreement<2>();
        check_collective_retention_agreement<3>();
    };
}

} // namespace rift_test::mpi::state_retention_agreement_00
