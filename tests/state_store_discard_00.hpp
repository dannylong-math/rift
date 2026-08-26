#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>
#include <stdexcept>

namespace rift_test::state_store_discard_00 {

template<int dim> void check_discard()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto store = rift::test::make_state_store(space);
    const auto accepted = store.snapshot(rift::StateSlot::accepted);

    auto transaction = store.begin_trial_collective(accepted.stamp().snapshot).value();
    const auto candidate = transaction.seal_collective().value();
    expect(store.discard_collective(candidate.stamp().snapshot).has_value());

    bool candidate_gone = false;
    try {
        static_cast<void>(store.snapshot(candidate.stamp().snapshot));
    }
    catch (const std::out_of_range&) {
        candidate_gone = true;
    }

    const auto accepted_result = store.discard_collective(accepted.stamp().snapshot);

    expect(candidate_gone);
    expect(!accepted_result.has_value());
    expect(accepted_result.error().code == rift::StateTransitionErrorCode::invalid_discard);
}

} // namespace rift_test::state_store_discard_00

namespace rift_test::state_store_discard_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "discard removes a private snapshot but cannot remove accepted state in 2D and 3D"_test = [] {
        check_discard<2>();
        check_discard<3>();
    };
}

} // namespace rift_test::state_store_discard_00
