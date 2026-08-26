#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>
namespace rift_test::state_transaction_isolation_00 {

template<int dim> void check_trial_isolation()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto store = rift::test::make_state_store(space);
    const auto base = store.snapshot(rift::StateSlot::accepted);
    const auto field = rift::test::field_reference(space, space.field_spaces().front().id());

    auto transaction = store.begin_trial_collective(base.stamp().snapshot).value();
    transaction.field(field).value().get() = 3.0;
    const auto candidate = transaction.seal_collective().value();

    expect(base.field(field).value().get().l2_norm() == 0.0_d);
    expect(candidate.field(field).value().get().l2_norm() > 0.0_d);
    expect(candidate.stamp().snapshot != base.stamp().snapshot);
    expect(!candidate.stamp().published_epoch.has_value());
    expect(!transaction.active());

    const auto access = transaction.field(field);
    expect(!access.has_value());
    expect(access.error().code == rift::StateTransitionErrorCode::inactive_transaction);
}

} // namespace rift_test::state_transaction_isolation_00

namespace rift_test::state_transaction_isolation_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "sealing a trial leaves its immutable base unchanged in 2D and 3D"_test = [] {
        check_trial_isolation<2>();
        check_trial_isolation<3>();
    };
}

} // namespace rift_test::state_transaction_isolation_00
