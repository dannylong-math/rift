#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>

namespace rift_test::state_transaction_abandon_00 {

template<int dim> void check_abandon()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto store = rift::test::make_state_store(space);
    const auto accepted = store.snapshot(rift::StateSlot::accepted);
    const auto field = rift::test::field_reference(space, space.field_spaces().front().id());

    auto transaction = store.begin_trial_collective(accepted.stamp().snapshot).value();
    transaction.field(field).value().get() = 4.0;
    transaction.abandon();
    transaction.abandon();

    expect(!transaction.active());
    expect(store.snapshot(rift::StateSlot::accepted).field(field).value().get().l2_norm() == 0.0_d);
}

} // namespace rift_test::state_transaction_abandon_00

namespace rift_test::state_transaction_abandon_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "abandoning a trial discards its mutable values in 2D and 3D"_test = [] {
        check_abandon<2>();
        check_abandon<3>();
    };
}

} // namespace rift_test::state_transaction_abandon_00
