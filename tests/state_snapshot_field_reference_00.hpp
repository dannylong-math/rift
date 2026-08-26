#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>
#include <rift/phase_graph.hpp>

namespace rift_test::state_snapshot_field_reference_00 {

template<int dim> void check_reference_validation()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto store = rift::make_state_store(space.layout(), {}).value();
    const auto snapshot = store.snapshot(rift::StateSlot::accepted);
    const auto reference =
        rift::test::require_optional(space.layout().field_reference(space.field_spaces().front().id()));
    auto wrong = reference;
    wrong.space.epoch = rift::SpaceEpoch::from_index(reference.space.epoch.value() + 1U);
    auto wrong_phase = reference;
    if (wrong_phase.phase) {
        wrong_phase.phase->phase = rift::PhaseId::from_index(wrong_phase.phase->phase.value() + 1U);
    }
    auto unknown = reference;
    unknown.group = rift::FieldGroupId::from_index(9999);

    expect(!space.layout().field_reference(unknown.group).has_value());
    expect(!(reference == wrong));
    expect(!(reference == wrong_phase));
    expect(!(reference == unknown));
    expect(snapshot.field(reference).has_value());
    expect(!snapshot.field(wrong).has_value());
    expect(snapshot.field(wrong).error().code == rift::StateTransitionErrorCode::field_reference_mismatch);
    expect(snapshot.field(wrong_phase).error().code == rift::StateTransitionErrorCode::field_reference_mismatch);
    expect(snapshot.field(unknown).error().code == rift::StateTransitionErrorCode::unknown_field);

    auto transaction = store.begin_trial_collective(snapshot.stamp().snapshot).value();
    expect(transaction.field(reference).has_value());
    expect(transaction.field(wrong).error().code == rift::StateTransitionErrorCode::field_reference_mismatch);
    expect(transaction.field(wrong_phase).error().code == rift::StateTransitionErrorCode::field_reference_mismatch);
    expect(transaction.field(unknown).error().code == rift::StateTransitionErrorCode::unknown_field);
}

} // namespace rift_test::state_snapshot_field_reference_00

namespace rift_test::state_snapshot_field_reference_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "local field lookup validates complete 2D and 3D provenance"_test = [] {
        check_reference_validation<2>();
        check_reference_validation<3>();
    };
}

} // namespace rift_test::state_snapshot_field_reference_00
