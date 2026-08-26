#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>

namespace rift_test::state_store_initial_00 {

template<int dim> void check_initial_state()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>({{"pressure"}});
    const auto store = rift::test::make_state_store(space);
    const auto accepted = store.snapshot(rift::StateSlot::accepted);
    const auto flow = rift::test::field_reference(space, space.field_spaces().front().id());
    const auto level_set = rift::test::field_reference(space, space.level_set_space().id());

    expect(accepted.stamp().space == space.provenance());
    expect(accepted.stamp().published_epoch.has_value());
    expect(accepted.field(flow).value().get().l2_norm() == 0.0_d);
    expect(accepted.field(level_set).value().get().l2_norm() == 0.0_d);
}

} // namespace rift_test::state_store_initial_00

namespace rift_test::state_store_initial_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "a new store publishes a zero-initialized accepted snapshot in 2D and 3D"_test = [] {
        check_initial_state<2>();
        check_initial_state<3>();
    };
}

} // namespace rift_test::state_store_initial_00
