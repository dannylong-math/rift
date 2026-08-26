#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
namespace rift_test::space_snapshot_missing_field_00 {

template<int dim> void check_missing_field_lookup()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    const auto gas = space.field_spaces().front().phase();

    expect(!space.find_field(gas, "temperature").has_value());
}

} // namespace rift_test::space_snapshot_missing_field_00

namespace rift_test::space_snapshot_missing_field_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "an absent phase field name returns no identity in 2D and 3D"_test = [] {
        check_missing_field_lookup<2>();
        check_missing_field_lookup<3>();
    };
}

} // namespace rift_test::space_snapshot_missing_field_00
