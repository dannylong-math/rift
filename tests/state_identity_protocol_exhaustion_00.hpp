#pragma once

#include "discrete_state_test_support.hpp"

#include <array>
#include <boost/ut.hpp>
#include <cstddef>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <limits>
#include <rift/discrete_state.hpp>

namespace rift_test::state_identity_protocol_exhaustion_00 {

class IdentitySequenceOverride {
public:
    explicit IdentitySequenceOverride(std::array<std::uint64_t, 5> replacement) :
        previous_(rift::detail::StateStoreAccess::replace_identity_sequences_for_test(replacement))
    {
    }

    IdentitySequenceOverride(const IdentitySequenceOverride&) = delete;
    IdentitySequenceOverride& operator=(const IdentitySequenceOverride&) = delete;
    IdentitySequenceOverride(IdentitySequenceOverride&&) = delete;
    IdentitySequenceOverride& operator=(IdentitySequenceOverride&&) = delete;
    ~IdentitySequenceOverride()
    {
        static_cast<void>(rift::detail::StateStoreAccess::replace_identity_sequences_for_test(previous_));
    }

private:
    std::array<std::uint64_t, 5> previous_;
};

inline std::array<std::uint64_t, 5> exhausted_component(const std::size_t component)
{
    std::array<std::uint64_t, 5> next{};
    next.at(component) = std::numeric_limits<std::uint32_t>::max();
    return next;
}

template<int dim> void check_protocol_exhaustion()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    {
        const IdentitySequenceOverride exhausted(exhausted_component(0));
        const auto result = rift::make_state_store(space.layout(), {});
        expect(!result.has_value());
        expect(result.error().code == rift::StateTransitionErrorCode::identity_exhausted);
    }

    auto store = rift::test::make_state_store(space);
    const auto accepted = store.snapshot(rift::StateSlot::accepted);
    {
        const IdentitySequenceOverride exhausted(exhausted_component(1));
        const auto result = store.begin_trial_collective(accepted.stamp().snapshot);
        expect(!result.has_value());
        expect(result.error().code == rift::StateTransitionErrorCode::identity_exhausted);
    }

    auto trial = store.begin_trial_collective(accepted.stamp().snapshot).value();
    {
        const IdentitySequenceOverride exhausted(exhausted_component(2));
        const auto result = trial.seal_collective();
        expect(!result.has_value());
        expect(result.error().code == rift::StateTransitionErrorCode::identity_exhausted);
        expect(trial.active());
        expect(store.snapshot(rift::StateSlot::accepted).stamp().snapshot == accepted.stamp().snapshot);
    }
    const auto candidate = trial.seal_collective().value();
    expect(candidate.stamp().snapshot.value() == accepted.stamp().snapshot.value() + 1U);
    {
        const IdentitySequenceOverride exhausted(exhausted_component(3));
        const auto result = store.publish_collective(candidate.stamp().snapshot);
        expect(!result.has_value());
        expect(result.error().code == rift::StateTransitionErrorCode::identity_exhausted);
    }
    const auto published = store.publish_collective(candidate.stamp().snapshot).value();

    auto geometry_trial = store.begin_trial_collective(published.stamp().snapshot).value();
    const auto level_set = rift::test::field_reference(space, space.level_set_space().id());
    geometry_trial.field(level_set).value().get() = 1.0;
    {
        const IdentitySequenceOverride exhausted(exhausted_component(4));
        const auto result = geometry_trial.seal_collective();
        expect(!result.has_value());
        expect(result.error().code == rift::StateTransitionErrorCode::identity_exhausted);
        expect(geometry_trial.active());
        expect(store.snapshot(rift::StateSlot::accepted).stamp().snapshot == published.stamp().snapshot);
    }
    const auto geometry_candidate = geometry_trial.seal_collective().value();
    expect(geometry_candidate.level_set_snapshot() != published.level_set_snapshot());
    expect(geometry_candidate.level_set_snapshot().value() == published.level_set_snapshot().value() + 1U);
}

} // namespace rift_test::state_identity_protocol_exhaustion_00

namespace rift_test::state_identity_protocol_exhaustion_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "every state protocol stage reports atomic identity exhaustion in 2D and 3D"_test = [] {
        check_protocol_exhaustion<2>();
        check_protocol_exhaustion<3>();
    };
}

} // namespace rift_test::state_identity_protocol_exhaustion_00
