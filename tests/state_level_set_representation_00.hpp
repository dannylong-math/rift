#pragma once

#include "discrete_state_test_support.hpp"

#include <array>
#include <bit>
#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <limits>
#include <rift/discrete_state.hpp>

namespace rift_test::state_level_set_representation_00 {

class SequenceOverride {
public:
    explicit SequenceOverride(const std::array<std::uint64_t, 5>& replacement) :
        original_(rift::detail::StateStoreAccess::replace_identity_sequences_for_test(replacement))
    {
    }

    SequenceOverride(const SequenceOverride&) = delete;
    SequenceOverride& operator=(const SequenceOverride&) = delete;
    SequenceOverride(SequenceOverride&&) = delete;
    SequenceOverride& operator=(SequenceOverride&&) = delete;
    ~SequenceOverride()
    {
        static_cast<void>(rift::detail::StateStoreAccess::replace_identity_sequences_for_test(original_));
    }

private:
    std::array<std::uint64_t, 5> original_;
};

template<int dim> void check_level_set_representation_and_exhaustion()
{
    using namespace boost::ut;
    constexpr std::uint64_t positive_zero = 0x0000000000000000ULL;
    constexpr std::uint64_t negative_zero = 0x8000000000000000ULL;
    constexpr std::uint64_t subnormal = 0x0000000000000001ULL;
    constexpr std::uint64_t finite = 0xc005bf0a8b145769ULL;
    constexpr std::uint64_t infinity = 0x7ff0000000000000ULL;
    constexpr std::uint64_t negative_infinity = 0xfff0000000000000ULL;
    constexpr std::uint64_t nan_a = 0x7ff8000000000042ULL;
    constexpr std::uint64_t negative_nan_a = 0xfff8000000000042ULL;
    constexpr std::uint64_t nan_b = 0x7ff8000000000043ULL;
    constexpr std::uint64_t signaling_nan_a = 0x7ff0000000000042ULL;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    const auto level_set = rift::test::field_reference(space, space.level_set_space().id());
    auto store = rift::test::make_state_store(space, rift::StateRetentionPolicy{.max_pinned_private_snapshots = 1});
    const auto base = store.snapshot(rift::StateSlot::accepted);

    const auto seal_bits = [&](const rift::StateSnapshot& source, const std::uint64_t bits) {
        auto trial = store.begin_trial_collective(source.stamp().snapshot).value();
        auto& values = trial.field(level_set).value().get();
        values.local_element(values.locally_owned_size() - 1U) = std::bit_cast<double>(bits);
        return trial.seal_collective().value();
    };

    const auto unchanged_zero = seal_bits(base, positive_zero);
    expect(unchanged_zero.level_set_snapshot() == base.level_set_snapshot());
    const auto changed_zero = seal_bits(base, negative_zero);
    expect(changed_zero.level_set_snapshot() != base.level_set_snapshot());
    const auto changed_subnormal = seal_bits(base, subnormal);
    expect(changed_subnormal.level_set_snapshot() != base.level_set_snapshot());
    const auto changed_finite = seal_bits(base, finite);
    expect(changed_finite.level_set_snapshot() != base.level_set_snapshot());
    const auto changed_infinity = seal_bits(base, infinity);
    expect(changed_infinity.level_set_snapshot() != base.level_set_snapshot());
    expect(store.pin_collective(changed_infinity.stamp().snapshot).has_value());
    const auto unchanged_infinity = seal_bits(changed_infinity, infinity);
    expect(unchanged_infinity.level_set_snapshot() == changed_infinity.level_set_snapshot());
    const auto changed_infinity_sign = seal_bits(changed_infinity, negative_infinity);
    expect(changed_infinity_sign.level_set_snapshot() != changed_infinity.level_set_snapshot());
    const auto unchanged_negative_infinity = seal_bits(changed_infinity_sign, negative_infinity);
    expect(unchanged_negative_infinity.level_set_snapshot() == changed_infinity_sign.level_set_snapshot());
    const auto nan_candidate = seal_bits(base, nan_a);
    expect(nan_candidate.level_set_snapshot() != base.level_set_snapshot());
    const auto nan_accepted = store.publish_collective(nan_candidate.stamp().snapshot).value();

    const auto unchanged_nan = seal_bits(nan_accepted, nan_a);
    expect(unchanged_nan.level_set_snapshot() == nan_accepted.level_set_snapshot());
    const auto changed_nan_sign = seal_bits(nan_accepted, negative_nan_a);
    expect(changed_nan_sign.level_set_snapshot() != nan_accepted.level_set_snapshot());
    const auto unchanged_negative_nan = seal_bits(changed_nan_sign, negative_nan_a);
    expect(unchanged_negative_nan.level_set_snapshot() == changed_nan_sign.level_set_snapshot());
    const auto changed_nan_quiet_bit = seal_bits(nan_accepted, signaling_nan_a);
    expect(changed_nan_quiet_bit.level_set_snapshot() != nan_accepted.level_set_snapshot());
    const auto unchanged_signaling_nan = seal_bits(changed_nan_quiet_bit, signaling_nan_a);
    expect(unchanged_signaling_nan.level_set_snapshot() == changed_nan_quiet_bit.level_set_snapshot());

    auto exhausted_trial = store.begin_trial_collective(nan_accepted.stamp().snapshot).value();
    auto& exhausted_values = exhausted_trial.field(level_set).value().get();
    exhausted_values.local_element(exhausted_values.locally_owned_size() - 1U) = std::bit_cast<double>(nan_b);
    const auto exhausted = static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
    const std::array<std::uint64_t, 5> sentinel{17, 18, 19, 20, exhausted};
    {
        const SequenceOverride override(sentinel);
        const auto failed = exhausted_trial.seal_collective();
        expect(!failed.has_value());
        expect(failed.error().code == rift::StateTransitionErrorCode::identity_exhausted);
        expect(exhausted_trial.active());
        expect(std::bit_cast<std::uint64_t>(
                   exhausted_values.local_element(exhausted_values.locally_owned_size() - 1U)) == nan_b);
        const auto observed = rift::detail::StateStoreAccess::replace_identity_sequences_for_test(sentinel);
        expect(observed == sentinel);
    }
    const auto changed_nan = exhausted_trial.seal_collective().value();
    expect(changed_nan.level_set_snapshot() != nan_accepted.level_set_snapshot());
}

} // namespace rift_test::state_level_set_representation_00

namespace rift_test::state_level_set_representation_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "level-set revisions use exact binary64 representations and retry exhaustion"_test = [] {
        check_level_set_representation_and_exhaustion<2>();
        check_level_set_representation_and_exhaustion<3>();
    };
}

} // namespace rift_test::state_level_set_representation_00
