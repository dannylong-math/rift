#pragma once

#include "discrete_state_test_support.hpp"
#include "state_result_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>
#include <type_traits>
#include <utility>

namespace rift_test::state_store_collective_api_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "a state store is a movable nonassignable collective authority"_test = [] {
        static_assert(std::is_move_constructible_v<rift::StateStore>);
        static_assert(!std::is_copy_constructible_v<rift::StateStore>);
        static_assert(!std::is_copy_assignable_v<rift::StateStore>);
        static_assert(!std::is_move_assignable_v<rift::StateStore>);

        const auto space = rift::test::make_space_with_one_phase_field<2>();
        auto result =
            rift::make_state_store(space.layout(), rift::StateRetentionPolicy{.max_pinned_private_snapshots = 0});
        expect(result.has_value());
        auto store = rift::test::require_state_result(std::move(result));
        expect(store.id() == store.snapshot(rift::StateSlot::accepted).stamp().store);
        expect(store.provenance() == space.provenance());
        expect(store.space_epoch() == space.epoch());
        expect(store.retention_policy() == rift::StateRetentionPolicy{});
        expect(space.layout().space_epoch() == space.epoch());
    };
}

} // namespace rift_test::state_store_collective_api_00
