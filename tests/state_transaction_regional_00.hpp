#pragma once

#include "discrete_state_test_support.hpp"

#include <bit>
#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>
#include <stdexcept>

namespace rift_test::state_transaction_regional_00 {

template<int dim> void check_exact_regional_api()
{
    using namespace boost::ut;

    constexpr std::uint64_t first_bits = 0x3ff0000000000001ULL;
    constexpr std::uint64_t last_bits = 0xfff8000000000042ULL;
    constexpr std::uint64_t negative_zero_bits = 0x8000000000000000ULL;
    const auto space = rift::test::make_space_with_one_phase_field<dim>({{"pressure"}, {"constraint"}});
    const auto first = space.layout().regional_entries().front().id;
    const auto second = space.layout().regional_entries().back().id;
    auto store = rift::test::make_state_store(space);
    const auto base = store.snapshot(rift::StateSlot::accepted);

    expect(std::bit_cast<std::uint64_t>(base.regional_value(first)) == std::uint64_t{0});
    expect(throws<std::out_of_range>(
        [&] { static_cast<void>(base.regional_value(rift::RegionalEntryId::from_index(2))); }));

    auto trial = store.begin_trial_collective(base.stamp().snapshot).value();
    expect(trial.set_regional_value_collective(first, std::bit_cast<double>(first_bits)).has_value());
    expect(trial.set_regional_value_collective(second, std::bit_cast<double>(negative_zero_bits)).has_value());
    expect(trial.set_regional_value_collective(first, std::bit_cast<double>(last_bits)).has_value());
    expect(std::bit_cast<std::uint64_t>(base.regional_value(first)) == std::uint64_t{0});

    const auto candidate = trial.seal_collective().value();
    expect(std::bit_cast<std::uint64_t>(candidate.regional_value(first)) == last_bits);
    expect(std::bit_cast<std::uint64_t>(candidate.regional_value(second)) == negative_zero_bits);
    expect(candidate.level_set_snapshot() == base.level_set_snapshot());
}

} // namespace rift_test::state_transaction_regional_00

namespace rift_test::state_transaction_regional_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "regional state preserves exact representations in 2D and 3D"_test = [] {
        check_exact_regional_api<2>();
        check_exact_regional_api<3>();
    };
}

} // namespace rift_test::state_transaction_regional_00
