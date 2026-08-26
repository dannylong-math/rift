#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>
#include <utility>

namespace rift_test::state_transaction_move_00 {

template<int dim> void check_transaction_moves()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto store = rift::test::make_state_store(space);
    const auto accepted = store.snapshot(rift::StateSlot::accepted);
    const auto field = rift::test::field_reference(space, space.field_spaces().front().id());

    auto original = store.begin_trial_collective(accepted.stamp().snapshot).value();
    original.field(field).value().get() = 6.0;
    auto* const original_state = &original;
    auto moved = std::move(*original_state);
    // The moved-from transaction has a documented observable inactive state.
    expect(!original_state->active());
    expect(moved.active());
    const auto moved_from_seal = original_state->seal_collective();
    expect(!moved_from_seal.has_value());
    expect(moved_from_seal.error().code == rift::StateTransitionErrorCode::inactive_transaction);

    auto destination = store.begin_trial_collective(accepted.stamp().snapshot).value();
    auto* const moved_state = &moved;
    destination = std::move(*moved_state);
    // Move assignment gives the source the same documented inactive state.
    expect(!moved_state->active());
    expect(destination.active());
    const auto self_move = [](auto& transaction) { transaction = std::move(transaction); };
    self_move(destination);
    expect(destination.active());

    const auto candidate = destination.seal_collective().value();
    expect(candidate.field(field).value().get().l2_norm() > 0.0_d);
    const auto repeated_seal = destination.seal_collective();
    expect(!repeated_seal.has_value());
    expect(repeated_seal.error().code == rift::StateTransitionErrorCode::inactive_transaction);
}

} // namespace rift_test::state_transaction_move_00

namespace rift_test::state_transaction_move_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "moving a transaction transfers its sole mutable authority in 2D and 3D"_test = [] {
        check_transaction_moves<2>();
        check_transaction_moves<3>();
    };
}

} // namespace rift_test::state_transaction_move_00
