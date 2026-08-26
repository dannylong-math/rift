#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <optional>
#include <rift/discrete_state.hpp>
#include <type_traits>

static_assert(!std::is_default_constructible_v<rift::StateSnapshotStamp>);

namespace rift_test::state_snapshot_full_stamp_00 {

template<int dim> void check_complete_stamp_lifecycle()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto store = rift::test::make_state_store(space);
    const auto initial = store.snapshot(rift::StateSlot::accepted);
    const auto initial_stamp = initial.stamp();

    expect(initial_stamp.space == space.provenance());
    expect(initial_stamp.store == store.id());
    expect(initial_stamp.published_epoch.has_value());

    const rift::StateSnapshotStamp direct(space.provenance(), initial_stamp.store,
                                          rift::StateSnapshotId::from_index(initial_stamp.snapshot.value() + 1),
                                          std::nullopt);
    expect(direct.space == space.provenance());
    expect(direct.store == initial_stamp.store);
    expect(direct.snapshot.value() == initial_stamp.snapshot.value() + 1);
    expect(!direct.published_epoch.has_value());

    auto transaction = store.begin_trial_collective(initial_stamp.snapshot).value();
    const auto candidate = transaction.seal_collective().value();
    const auto candidate_stamp = candidate.stamp();
    expect(candidate_stamp.space == space.provenance());
    expect(candidate_stamp.store == initial_stamp.store);
    expect(candidate_stamp.snapshot != initial_stamp.snapshot);
    expect(!candidate_stamp.published_epoch.has_value());

    const auto published = store.publish_collective(candidate_stamp.snapshot).value();
    const auto published_stamp = published.stamp();
    expect(published_stamp.space == space.provenance());
    expect(published_stamp.store == initial_stamp.store);
    expect(published_stamp.snapshot == candidate_stamp.snapshot);
    expect(published_stamp.published_epoch.has_value());
}

} // namespace rift_test::state_snapshot_full_stamp_00

namespace rift_test::state_snapshot_full_stamp_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "state snapshot stamps"_test = [] {
        "complete construction and production paths preserve every 2D and 3D identity"_test = [] {
            check_complete_stamp_lifecycle<2>();
            check_complete_stamp_lifecycle<3>();
        };
    };
}

} // namespace rift_test::state_snapshot_full_stamp_00
