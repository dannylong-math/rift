#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>

namespace rift_test::state_transaction_level_set_00 {

template<int dim> void check_level_set_identity()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto store = rift::test::make_state_store(space);
    const auto initial = store.snapshot(rift::StateSlot::accepted);
    const auto flow = rift::test::field_reference(space, space.field_spaces().front().id());
    const auto level_set = rift::test::field_reference(space, space.level_set_space().id());

    auto field_trial = store.begin_trial_collective(initial.stamp().snapshot).value();
    field_trial.field(flow).value().get() = 1.0;
    const auto field_candidate = field_trial.seal_collective().value();
    expect(field_candidate.level_set_snapshot() == initial.level_set_snapshot());

    auto unchanged_level_set_trial = store.begin_trial_collective(field_candidate.stamp().snapshot).value();
    unchanged_level_set_trial.field(level_set).value().get() = 0.0;
    const auto unchanged_level_set = unchanged_level_set_trial.seal_collective().value();
    expect(unchanged_level_set.level_set_snapshot() == initial.level_set_snapshot());

    auto level_set_trial = store.begin_trial_collective(unchanged_level_set.stamp().snapshot).value();
    level_set_trial.field(level_set).value().get() = 2.0;
    const auto changed_level_set = level_set_trial.seal_collective().value();
    expect(changed_level_set.level_set_snapshot() != initial.level_set_snapshot());
}

} // namespace rift_test::state_transaction_level_set_00

namespace rift_test::state_transaction_level_set_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "the level-set snapshot identity changes only with level-set values in 2D and 3D"_test = [] {
        check_level_set_identity<2>();
        check_level_set_identity<3>();
    };
}

} // namespace rift_test::state_transaction_level_set_00
