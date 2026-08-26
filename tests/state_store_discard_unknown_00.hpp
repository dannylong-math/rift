#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>

namespace rift_test::state_store_discard_unknown_00 {

template<int dim> void check_unknown_discard_rejected()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto store = rift::test::make_state_store(space);
    const auto result = store.discard_collective(rift::StateSnapshotId::from_index(999999));
    expect(!result.has_value());
    expect(result.error().code == rift::StateTransitionErrorCode::unknown_snapshot);
}

} // namespace rift_test::state_store_discard_unknown_00

namespace rift_test::state_store_discard_unknown_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "discard rejects an unknown snapshot identity in 2D and 3D"_test = [] {
        check_unknown_discard_rejected<2>();
        check_unknown_discard_rejected<3>();
    };
}

} // namespace rift_test::state_store_discard_unknown_00
