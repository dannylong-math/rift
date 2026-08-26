#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>
#include <utility>

namespace rift_test::state_store_move_transaction_00 {

template<int dim> void check_live_transaction_survives_store_move()
{
    using namespace boost::ut;
    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto store = rift::test::make_state_store(space);
    const auto field = rift::test::field_reference(space, space.field_spaces().front().id());
    const auto accepted = store.snapshot(rift::StateSlot::accepted);
    auto trial = store.begin_trial_collective(accepted.stamp().snapshot).value();
    trial.field(field).value().get() = 9.0;

    rift::StateStore moved(std::move(store));
    expect(trial.active());
    const auto candidate = trial.seal_collective().value();
    const auto published = moved.publish_collective(candidate.stamp().snapshot).value();
    expect(published.field(field).value().get().l2_norm() > 0.0_d);
}

} // namespace rift_test::state_store_move_transaction_00

namespace rift_test::state_store_move_transaction_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "moving a store preserves live transaction authority in 2D and 3D"_test = [] {
        check_live_transaction_survives_store_move<2>();
        check_live_transaction_survives_store_move<3>();
    };
}

} // namespace rift_test::state_store_move_transaction_00
