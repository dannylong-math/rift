#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>

namespace rift_test::state_transaction_publish_00 {

template<int dim> void check_publication()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto store = rift::test::make_state_store(space);
    const auto initial = store.snapshot(rift::StateSlot::accepted);
    const auto field = rift::test::field_reference(space, space.field_spaces().front().id());

    auto transaction = store.begin_trial_collective(initial.stamp().snapshot).value();
    transaction.field(field).value().get() = 2.0;
    const auto candidate = transaction.seal_collective().value();
    const auto accepted = store.publish_collective(candidate.stamp().snapshot).value();
    const auto previous = store.snapshot(rift::StateSlot::previous);

    expect(accepted.stamp().published_epoch.has_value());
    expect(accepted.stamp().published_epoch != initial.stamp().published_epoch);
    expect(accepted.stamp().snapshot == candidate.stamp().snapshot);
    expect(previous.stamp().snapshot == initial.stamp().snapshot);
    expect(previous.field(field).value().get().l2_norm() == 0.0_d);
    expect(accepted.field(field).value().get().l2_norm() > 0.0_d);
}

} // namespace rift_test::state_transaction_publish_00

namespace rift_test::state_transaction_publish_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "publishing a candidate advances the accepted epoch and retains the previous state in 2D and 3D"_test = [] {
        check_publication<2>();
        check_publication<3>();
    };
}

} // namespace rift_test::state_transaction_publish_00
