#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <deal.II/base/types.h>
#include <rift/discrete_state.hpp>
#include <stdexcept>

namespace rift_test::state_store_lifetime_00 {

struct ExternalSnapshots {
    rift::StateFieldReference field;
    rift::StateSnapshot initial;
    rift::StateSnapshot discarded;
    rift::StateSnapshot evicted;
    rift::StateSnapshot accepted;
};

inline void expect_constant(const rift::StateSnapshot& snapshot, const rift::StateFieldReference field,
                            const double expected)
{
    using namespace boost::ut;
    const auto& values = snapshot.field(field).value().get();
    expect(values.locally_owned_size() > 0U);
    for (dealii::types::global_dof_index index = 0; index < values.locally_owned_size(); ++index) {
        expect(values.local_element(index) == expected);
    }
}

template<int dim> void check_external_snapshot_lifetime()
{
    using namespace boost::ut;
    const auto external = [] {
        const auto space = rift::test::make_space_with_one_phase_field<dim>();
        const auto field = rift::test::field_reference(space, space.field_spaces().front().id());
        auto store = rift::test::make_state_store(space);
        const auto initial = store.snapshot(rift::StateSlot::accepted);

        auto discarded_trial = store.begin_trial_collective(initial.stamp().snapshot).value();
        discarded_trial.field(field).value().get() = 2.0;
        const auto discarded = discarded_trial.seal_collective().value();
        expect(store.discard_collective(discarded.stamp().snapshot).has_value());
        expect(throws<std::out_of_range>([&] { static_cast<void>(store.snapshot(discarded.stamp().snapshot)); }));

        auto first_trial = store.begin_trial_collective(initial.stamp().snapshot).value();
        first_trial.field(field).value().get() = 3.0;
        const auto first_candidate = first_trial.seal_collective().value();
        const auto first = store.publish_collective(first_candidate.stamp().snapshot).value();
        auto second_trial = store.begin_trial_collective(first.stamp().snapshot).value();
        second_trial.field(field).value().get() = 4.0;
        const auto second_candidate = second_trial.seal_collective().value();
        const auto second = store.publish_collective(second_candidate.stamp().snapshot).value();
        auto third_trial = store.begin_trial_collective(second.stamp().snapshot).value();
        third_trial.field(field).value().get() = 5.0;
        const auto third_candidate = third_trial.seal_collective().value();
        const auto third = store.publish_collective(third_candidate.stamp().snapshot).value();
        expect(throws<std::out_of_range>([&] { static_cast<void>(store.snapshot(first.stamp().snapshot)); }));
        return ExternalSnapshots{field, initial, discarded, first, third};
    }();

    expect_constant(external.initial, external.field, 0.0);
    expect_constant(external.discarded, external.field, 2.0);
    expect_constant(external.evicted, external.field, 3.0);
    expect_constant(external.accepted, external.field, 5.0);
}

} // namespace rift_test::state_store_lifetime_00

namespace rift_test::state_store_lifetime_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "an immutable snapshot outlives its state store in 2D and 3D"_test = [] {
        check_external_snapshot_lifetime<2>();
        check_external_snapshot_lifetime<3>();
    };
}

} // namespace rift_test::state_store_lifetime_00
