#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>

namespace rift_test::state_store_space_epoch_00 {

template<int dim> void check_space_epoch()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    const auto store = rift::test::make_state_store(space);

    expect(store.space_epoch() == space.epoch());
}

} // namespace rift_test::state_store_space_epoch_00

namespace rift_test::state_store_space_epoch_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "the state store reports its finalized space epoch in 2D and 3D"_test = [] {
        check_space_epoch<2>();
        check_space_epoch<3>();
    };
}

} // namespace rift_test::state_store_space_epoch_00
