#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>

namespace rift_test::state_store_reject_republish_00 {

template<int dim> void check_republish_rejected()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto store = rift::test::make_state_store(space);
    const auto accepted = store.snapshot(rift::StateSlot::accepted);

    const auto result = store.publish_collective(accepted.stamp().snapshot);
    expect(!result.has_value());
    expect(result.error().code == rift::StateTransitionErrorCode::wrong_candidate_state);
}

} // namespace rift_test::state_store_reject_republish_00

namespace rift_test::state_store_reject_republish_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "an already published snapshot cannot be published again in 2D and 3D"_test = [] {
        check_republish_rejected<2>();
        check_republish_rejected<3>();
    };
}

} // namespace rift_test::state_store_reject_republish_00
