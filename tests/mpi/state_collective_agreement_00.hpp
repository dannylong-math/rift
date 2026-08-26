#pragma once

#include "../discrete_state_test_support.hpp"
#include "../state_result_test_support.hpp"
#include "state_collective_test_support.hpp"

#include <algorithm>
#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <deal.II/base/types.h>
#include <expected>
#include <mpi.h>
#include <mpi_proto.h>
#include <optional>
#include <rift/discrete_state.hpp>
#include <set>

namespace rift_test::mpi::state_collective_agreement_00 {

template<class Id> void expect_rank_agreement(const Id id)
{
    using namespace boost::ut;
    const auto values = dealii::Utilities::MPI::all_gather(MPI_COMM_WORLD, id.value());
    expect(std::ranges::all_of(values, [&](const auto value) { return value == values.front(); }));
}

template<int dim> void check_collective_state_protocol()
{
    using namespace boost::ut;
    const auto rank = dealii::Utilities::MPI::this_mpi_process(MPI_COMM_WORLD);
    const auto fixture = rift::test::make_distributed_state_fixture<dim>();
    const auto& space = fixture.space;
    const auto alternate_space = rift::test::make_distributed_state_space(fixture.mesh, fixture.graph, fixture.gas,
                                                                          fixture.owned, "alternate_flow");

    const auto& selected_layout = rank == 1U ? alternate_space.layout() : space.layout();
    const auto rejected_layout = rift::make_state_store(selected_layout, {});
    expect(!rejected_layout.has_value());
    expect(rejected_layout.error().code == rift::StateTransitionErrorCode::replicated_layout_mismatch);

    const rift::StateRetentionPolicy divergent{
        .max_pinned_private_snapshots = rank == 1U ? 1U : 0U,
    };
    const auto rejected_policy = rift::make_state_store(space.layout(), divergent);
    expect(!rejected_policy.has_value());
    expect(rejected_policy.error().code == rift::StateTransitionErrorCode::retention_policy_mismatch);

    auto store =
        rift::make_state_store(space.layout(), rift::StateRetentionPolicy{.max_pinned_private_snapshots = 1}).value();
    auto other_store = rift::make_state_store(space.layout(), {}).value();
    expect(store.id() != other_store.id());
    expect_rank_agreement(store.id());
    expect_rank_agreement(other_store.id());

    const auto initial = store.snapshot(rift::StateSlot::accepted);
    expect(initial.stamp().space == space.provenance());
    expect(initial.stamp().store == store.id());
    expect_rank_agreement(initial.stamp().snapshot);
    expect_rank_agreement(rift::test::require_state_result(initial.stamp().published_epoch));
    expect_rank_agreement(initial.level_set_snapshot());

    const auto field = rift::test::flow_reference(space);
    const auto& block = space.layout().field_blocks().front();
    const auto& initial_vector = initial.field(field).value().get();
    expect(initial_vector.locally_owned_elements() == block.locally_owned_dofs);
    expect(initial_vector.l2_norm() == 0.0_d);

    auto transaction = store.begin_trial_collective(initial.stamp().snapshot).value();
    auto& values = transaction.field(field).value().get();
    for (dealii::types::global_dof_index index = 0; index < values.locally_owned_size(); ++index) {
        values.local_element(index) = static_cast<double>(rank + 1U);
    }
    const auto locally_owned_size = values.locally_owned_size();
    const auto candidate = transaction.seal_collective().value();
    expect_rank_agreement(candidate.stamp().snapshot);
    const auto& candidate_values = candidate.field(field).value().get();
    expect(candidate_values.locally_owned_size() == locally_owned_size);
    const auto local_expected = static_cast<double>((rank + 1U) * locally_owned_size);
    double global_expected = 0.0;
    expect(MPI_Allreduce(&local_expected, &global_expected, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD) == MPI_SUCCESS);
    expect(candidate_values.l1_norm() == global_expected);

    const auto published = store.publish_collective(candidate.stamp().snapshot).value();
    expect(published.stamp().snapshot == candidate.stamp().snapshot);
    expect(published.stamp().published_epoch.has_value());
    expect(!candidate.stamp().published_epoch.has_value());
    expect_rank_agreement(rift::test::require_state_result(published.stamp().published_epoch));

    auto first = store.begin_trial_collective(published.stamp().snapshot).value();
    const auto mismatched_argument = rank == 1U
                                         ? rift::StateSnapshotId::from_index(published.stamp().snapshot.value() + 500U)
                                         : published.stamp().snapshot;
    const auto rejected_argument = store.begin_trial_collective(mismatched_argument);
    expect(!rejected_argument.has_value());
    expect(rejected_argument.error().code == rift::StateTransitionErrorCode::argument_mismatch);
    auto second = store.begin_trial_collective(published.stamp().snapshot).value();
    expect(second.id().value() == first.id().value() + 1U);

    auto& selected_store = rank == 1U ? other_store : store;
    const auto rejected_store =
        selected_store.begin_trial_collective(selected_store.snapshot(rift::StateSlot::accepted).stamp().snapshot);
    expect(!rejected_store.has_value());
    expect(rejected_store.error().code == rift::StateTransitionErrorCode::store_mismatch);

    const auto crossed_operation = [&]() -> rift::StateTransitionResult<void> {
        if (rank == 1U) {
            const auto result =
                other_store.publish_collective(other_store.snapshot(rift::StateSlot::accepted).stamp().snapshot);
            if (!result) {
                return std::unexpected(result.error());
            }
            return {};
        }
        const auto result = store.begin_trial_collective(published.stamp().snapshot);
        if (!result) {
            return std::unexpected(result.error());
        }
        return {};
    }();
    expect(!crossed_operation.has_value());
    expect(crossed_operation.error().code == rift::StateTransitionErrorCode::operation_mismatch);

    auto trial_a = store.begin_trial_collective(published.stamp().snapshot).value();
    auto trial_b = store.begin_trial_collective(published.stamp().snapshot).value();
    auto& selected_trial = rank == 1U ? trial_b : trial_a;
    const auto rejected_trial = selected_trial.seal_collective();
    expect(!rejected_trial.has_value());
    expect(rejected_trial.error().code == rift::StateTransitionErrorCode::transaction_mismatch);
    expect(trial_a.active());
    expect(trial_b.active());
    const auto mismatch_candidate_a = trial_a.seal_collective().value();
    const auto mismatch_candidate_b = trial_b.seal_collective().value();
    expect(mismatch_candidate_b.stamp().snapshot.value() == mismatch_candidate_a.stamp().snapshot.value() + 1U);

    auto inactive = store.begin_trial_collective(published.stamp().snapshot).value();
    if (rank == 1U) {
        inactive.abandon();
    }
    const auto rejected_inactive = inactive.seal_collective();
    expect(!rejected_inactive.has_value());
    expect(rejected_inactive.error().code == rift::StateTransitionErrorCode::inactive_transaction);

    auto sibling_a = store.begin_trial_collective(published.stamp().snapshot).value();
    auto sibling_b = store.begin_trial_collective(published.stamp().snapshot).value();
    const auto private_a = sibling_a.seal_collective().value();
    expect(store.pin_collective(private_a.stamp().snapshot).has_value());
    const auto private_b = sibling_b.seal_collective().value();
    expect(private_a.stamp().snapshot.value() == mismatch_candidate_b.stamp().snapshot.value() + 1U);
    const auto published_a = store.publish_collective(private_a.stamp().snapshot).value();
    const auto stale = store.publish_collective(private_b.stamp().snapshot);
    expect(!stale.has_value());
    expect(stale.error().code == rift::StateTransitionErrorCode::stale_accepted_root);
    expect(!store.snapshot(private_b.stamp().snapshot).stamp().published_epoch.has_value());
    expect(store.discard_collective(private_b.stamp().snapshot).has_value());
    auto consecutive_trial = store.begin_trial_collective(published_a.stamp().snapshot).value();
    const auto consecutive_candidate = consecutive_trial.seal_collective().value();
    const auto consecutive = store.publish_collective(consecutive_candidate.stamp().snapshot).value();
    expect(rift::test::require_state_result(consecutive.stamp().published_epoch).value() ==
           rift::test::require_state_result(published_a.stamp().published_epoch).value() + 1U);

    auto parent_trial = store.begin_trial_collective(consecutive.stamp().snapshot).value();
    const auto parent = parent_trial.seal_collective().value();
    auto child_trial = store.begin_trial_collective(parent.stamp().snapshot).value();
    expect(store.pin_collective(parent.stamp().snapshot).has_value());
    const auto child = child_trial.seal_collective().value();
    static_cast<void>(store.publish_collective(parent.stamp().snapshot).value());
    const auto stale_child = store.publish_collective(child.stamp().snapshot);
    expect(!stale_child.has_value());
    expect(stale_child.error().code == rift::StateTransitionErrorCode::stale_accepted_root);
    expect(store.discard_collective(child.stamp().snapshot).has_value());

    auto move_source = store.begin_trial_collective(store.snapshot(rift::StateSlot::accepted).stamp().snapshot).value();
    auto* const source_tombstone = &move_source;
    auto moved_trial = std::move(*source_tombstone);
    auto& selected_tombstone = rank == 1U ? *source_tombstone : moved_trial;
    const auto asymmetric_moved_from = selected_tombstone.seal_collective();
    expect(!asymmetric_moved_from.has_value());
    expect(asymmetric_moved_from.error().code == rift::StateTransitionErrorCode::inactive_transaction);
    const auto moved_candidate = moved_trial.seal_collective().value();
    expect(moved_candidate.stamp().snapshot.value() == child.stamp().snapshot.value() + 1U);
    const auto already_sealed = moved_trial.seal_collective();
    expect(!already_sealed.has_value());
    expect(already_sealed.error().code == rift::StateTransitionErrorCode::inactive_transaction);
    expect(store.discard_collective(moved_candidate.stamp().snapshot).has_value());

    auto expiring_store = std::optional<rift::StateStore>{rift::make_state_store(space.layout(), {}).value()};
    const auto expiring_initial = expiring_store->snapshot(rift::StateSlot::accepted);
    auto expired_trial = expiring_store->begin_trial_collective(expiring_initial.stamp().snapshot).value();
    if (rank == 1U) {
        expiring_store.reset();
    }
    const auto expired = expired_trial.seal_collective();
    expect(!expired.has_value());
    expect(expired.error().code == rift::StateTransitionErrorCode::expired_store);
    expiring_store.reset();
    auto retry_after_expiry = rift::make_state_store(space.layout(), {}).value();
    expect(retry_after_expiry.snapshot(rift::StateSlot::accepted).stamp().snapshot.value() ==
           expiring_initial.stamp().snapshot.value() + 1U);

    const auto serial_space = rift::test::make_space_with_one_phase_field<dim>();
    const auto self_store = rift::make_state_store(serial_space.layout(), {}).value();
    const auto self_ids = dealii::Utilities::MPI::all_gather(MPI_COMM_WORLD, self_store.id().value());
    expect(std::set<std::uint64_t>(self_ids.begin(), self_ids.end()).size() == self_ids.size());
}

inline void register_tests()
{
    using namespace boost::ut;
    "collective state descriptors and partitions agree in 2D and 3D"_test = [] {
        check_collective_state_protocol<2>();
        check_collective_state_protocol<3>();
    };
}

} // namespace rift_test::mpi::state_collective_agreement_00
