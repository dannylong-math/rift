#pragma once

#include "discrete_state_test_support.hpp"

#include <bit>
#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>
#include <tuple>

namespace rift_test::state_regional_retention_lifetime_00 {

template<int dim> void check_regional_retention_and_external_lifetime()
{
    using namespace boost::ut;
    constexpr std::uint64_t pinned_bits = 0x3ff0000000000001ULL;
    constexpr std::uint64_t evicted_bits = 0x4000000000000001ULL;
    constexpr std::uint64_t active_bits = 0x7ff8000000000042ULL;
    constexpr std::uint64_t discarded_bits = 0x8000000000000000ULL;

    const auto [entry, accepted, published, evicted, discarded] = [&] {
        const auto space = rift::test::make_space_with_one_phase_field<dim>({{"regional"}});
        const auto regional = space.layout().regional_entries().front().id;
        auto store = rift::test::make_state_store(space, rift::StateRetentionPolicy{.max_pinned_private_snapshots = 1});
        auto initial = store.snapshot(rift::StateSlot::accepted);

        auto pinned_trial = store.begin_trial_collective(initial.stamp().snapshot).value();
        expect(pinned_trial.set_regional_value_collective(regional, std::bit_cast<double>(pinned_bits)).has_value());
        auto pinned = pinned_trial.seal_collective().value();
        expect(store.pin_collective(pinned.stamp().snapshot).has_value());
        auto active_from_pinned = store.begin_trial_collective(pinned.stamp().snapshot).value();

        auto evicted_trial = store.begin_trial_collective(initial.stamp().snapshot).value();
        expect(evicted_trial.set_regional_value_collective(regional, std::bit_cast<double>(evicted_bits)).has_value());
        auto evicted_handle = evicted_trial.seal_collective().value();
        expect(store.unpin_collective(pinned.stamp().snapshot).has_value());

        expect(
            active_from_pinned.set_regional_value_collective(regional, std::bit_cast<double>(active_bits)).has_value());
        auto active_candidate = active_from_pinned.seal_collective().value();
        expect(store.pin_collective(active_candidate.stamp().snapshot).has_value());
        auto published_handle = store.publish_collective(active_candidate.stamp().snapshot).value();

        auto discarded_trial = store.begin_trial_collective(published_handle.stamp().snapshot).value();
        expect(
            discarded_trial.set_regional_value_collective(regional, std::bit_cast<double>(discarded_bits)).has_value());
        auto discarded_handle = discarded_trial.seal_collective().value();
        expect(store.discard_collective(discarded_handle.stamp().snapshot).has_value());
        return std::tuple{regional, std::move(initial), std::move(published_handle), std::move(evicted_handle),
                          std::move(discarded_handle)};
    }();

    expect(std::bit_cast<std::uint64_t>(accepted.regional_value(entry)) == std::uint64_t{0});
    expect(std::bit_cast<std::uint64_t>(published.regional_value(entry)) == active_bits);
    expect(std::bit_cast<std::uint64_t>(evicted.regional_value(entry)) == evicted_bits);
    expect(std::bit_cast<std::uint64_t>(discarded.regional_value(entry)) == discarded_bits);
}

} // namespace rift_test::state_regional_retention_lifetime_00

namespace rift_test::state_regional_retention_lifetime_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "regional values survive retention transitions and every owning store lifetime"_test = [] {
        check_regional_retention_and_external_lifetime<2>();
        check_regional_retention_and_external_lifetime<3>();
    };
}

} // namespace rift_test::state_regional_retention_lifetime_00
