#pragma once

#include "discrete_state_test_support.hpp"

#include <array>
#include <bit>
#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <limits>
#include <rift/discrete_state.hpp>

namespace rift_test::state_regional_representation_00 {

template<int dim> void check_regional_representation_corpus()
{
    using namespace boost::ut;
    constexpr std::array<std::uint64_t, 10> representations{
        0x0000000000000000ULL, 0x8000000000000000ULL, 0x0000000000000001ULL, 0xc005bf0a8b145769ULL,
        0x7ff0000000000000ULL, 0xfff0000000000000ULL, 0x7ff8000000000042ULL, 0xfff8000000000042ULL,
        0x7ff8000000000043ULL, 0x7ff0000000000042ULL,
    };
    const auto space = rift::test::make_space_with_one_phase_field<dim>({{"regional"}});
    const auto entry = space.layout().regional_entries().front().id;
    auto store = rift::test::make_state_store(space, rift::StateRetentionPolicy{.max_pinned_private_snapshots = 1});
    const auto base = store.snapshot(rift::StateSlot::accepted);

    for (const auto bits : representations) {
        auto trial = store.begin_trial_collective(base.stamp().snapshot).value();
        expect(trial.set_regional_value_collective(entry, std::bit_cast<double>(bits)).has_value());
        expect(rift::detail::StateStoreAccess::regional_cache_bits(trial, entry) == bits);
        expect(rift::detail::StateStoreAccess::regional_backend_bits(trial, entry) == bits);
        const auto candidate = trial.seal_collective().value();
        expect(std::bit_cast<std::uint64_t>(candidate.regional_value(entry)) == bits);
        expect(rift::detail::StateStoreAccess::regional_cache_bits(candidate, entry) == bits);
        expect(rift::detail::StateStoreAccess::regional_backend_bits(candidate, entry) == bits);
        expect(candidate.level_set_snapshot() == base.level_set_snapshot());
        expect(std::bit_cast<std::uint64_t>(base.regional_value(entry)) == std::uint64_t{0});
    }

    auto left_trial = store.begin_trial_collective(base.stamp().snapshot).value();
    auto right_trial = store.begin_trial_collective(base.stamp().snapshot).value();
    expect(left_trial.set_regional_value_collective(entry, std::bit_cast<double>(representations.at(3))).has_value());
    expect(right_trial.set_regional_value_collective(entry, std::bit_cast<double>(representations.at(6))).has_value());
    const auto left = left_trial.seal_collective().value();
    expect(store.pin_collective(left.stamp().snapshot).has_value());
    const auto right = right_trial.seal_collective().value();
    expect(std::bit_cast<std::uint64_t>(left.regional_value(entry)) == representations.at(3));
    expect(std::bit_cast<std::uint64_t>(right.regional_value(entry)) == representations.at(6));
}

} // namespace rift_test::state_regional_representation_00

namespace rift_test::state_regional_representation_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "regional scalars preserve every binary64 class and sibling isolation"_test = [] {
        static_assert(sizeof(double) == sizeof(std::uint64_t));
        static_assert(std::numeric_limits<double>::is_iec559);
        check_regional_representation_corpus<2>();
        check_regional_representation_corpus<3>();
    };
}

} // namespace rift_test::state_regional_representation_00
