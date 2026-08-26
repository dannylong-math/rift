#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>
#include <stdexcept>

namespace rift_test::state_store_previous_missing_00 {

template<int dim> void check_previous_missing()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    const auto store = rift::test::make_state_store(space);

    bool rejected = false;
    try {
        static_cast<void>(store.snapshot(rift::StateSlot::previous));
    }
    catch (const std::out_of_range&) {
        rejected = true;
    }
    expect(rejected);
}

} // namespace rift_test::state_store_previous_missing_00

namespace rift_test::state_store_previous_missing_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "a new store has no previous accepted snapshot in 2D and 3D"_test = [] {
        check_previous_missing<2>();
        check_previous_missing<3>();
    };
}

} // namespace rift_test::state_store_previous_missing_00
