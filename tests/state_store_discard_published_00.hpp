#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>
#include <stdexcept>

namespace rift_test::state_store_discard_published_00 {

template<int dim> void check_older_published_state_is_protected()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto store = rift::test::make_state_store(space);
    const auto initial = store.snapshot(rift::StateSlot::accepted);

    auto first_trial = store.begin_trial_collective(initial.stamp().snapshot).value();
    const auto first_candidate = first_trial.seal_collective().value();
    const auto first = store.publish_collective(first_candidate.stamp().snapshot).value();
    auto second_trial = store.begin_trial_collective(first.stamp().snapshot).value();
    const auto second_candidate = second_trial.seal_collective().value();
    static_cast<void>(store.publish_collective(second_candidate.stamp().snapshot).value());

    bool evicted = false;
    try {
        static_cast<void>(store.snapshot(initial.stamp().snapshot));
    }
    catch (const std::out_of_range&) {
        evicted = true;
    }

    expect(evicted);
    expect(initial.stamp().published_epoch.has_value());
}

} // namespace rift_test::state_store_discard_published_00

namespace rift_test::state_store_discard_published_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "publication evicts old metadata while external handles survive in 2D and 3D"_test = [] {
        check_older_published_state_is_protected<2>();
        check_older_published_state_is_protected<3>();
    };
}

} // namespace rift_test::state_store_discard_published_00
