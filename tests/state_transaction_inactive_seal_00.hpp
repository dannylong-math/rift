#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>

namespace rift_test::state_transaction_inactive_seal_00 {

template<int dim> void check_inactive_seal()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto store = rift::test::make_state_store(space);
    auto transaction = store.begin_trial_collective(store.snapshot(rift::StateSlot::accepted).stamp().snapshot).value();
    transaction.abandon();

    const auto result = transaction.seal_collective();
    expect(!result.has_value());
    expect(result.error().code == rift::StateTransitionErrorCode::inactive_transaction);
}

} // namespace rift_test::state_transaction_inactive_seal_00

namespace rift_test::state_transaction_inactive_seal_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "sealing is rejected after a transaction becomes inactive in 2D and 3D"_test = [] {
        check_inactive_seal<2>();
        check_inactive_seal<3>();
    };
}

} // namespace rift_test::state_transaction_inactive_seal_00
