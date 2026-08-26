#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>

namespace rift_test::state_transaction_inactive_regional_00 {

template<int dim> void check_expired_store_access()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    const auto field = rift::test::field_reference(space, space.field_spaces().front().id());
    auto transaction = [&] {
        auto store = rift::test::make_state_store(space);
        return store.begin_trial_collective(store.snapshot(rift::StateSlot::accepted).stamp().snapshot).value();
    }();

    expect(!transaction.active());
    const auto access = transaction.field(field);
    expect(!access.has_value());
    expect(access.error().code == rift::StateTransitionErrorCode::expired_store);
    const auto seal = transaction.seal_collective();
    expect(!seal.has_value());
    expect(seal.error().code == rift::StateTransitionErrorCode::expired_store);
}

} // namespace rift_test::state_transaction_inactive_regional_00

namespace rift_test::state_transaction_inactive_regional_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "field access and sealing reject an expired store in 2D and 3D"_test = [] {
        check_expired_store_access<2>();
        check_expired_store_access<3>();
    };
}

} // namespace rift_test::state_transaction_inactive_regional_00
