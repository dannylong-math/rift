#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>

namespace rift_test::state_store_reject_unknown_snapshot_00 {

template<int dim> void check_unknown_snapshot_rejected()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto store = rift::test::make_state_store(space);
    const auto unknown = rift::StateSnapshotId::from_index(999999);

    const auto begin_result = store.begin_trial_collective(unknown);
    const auto publish_result = store.publish_collective(unknown);

    expect(!begin_result.has_value());
    expect(begin_result.error().code == rift::StateTransitionErrorCode::unknown_snapshot);
    expect(!publish_result.has_value());
    expect(publish_result.error().code == rift::StateTransitionErrorCode::unknown_snapshot);
}

} // namespace rift_test::state_store_reject_unknown_snapshot_00

namespace rift_test::state_store_reject_unknown_snapshot_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "unknown snapshot identities are rejected before mutation or publication in 2D and 3D"_test = [] {
        check_unknown_snapshot_rejected<2>();
        check_unknown_snapshot_rejected<3>();
    };
}

} // namespace rift_test::state_store_reject_unknown_snapshot_00
