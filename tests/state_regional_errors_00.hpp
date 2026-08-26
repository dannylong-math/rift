#pragma once

#include "discrete_state_test_support.hpp"

#include <array>
#include <bit>
#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <limits>
#include <optional>
#include <rift/discrete_state.hpp>

namespace rift_test::state_regional_errors_00 {

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

inline void expect_sequences(const std::array<std::uint64_t, 5>& expected)
{
    using namespace boost::ut;
    const auto observed = rift::detail::StateStoreAccess::replace_identity_sequences_for_test(expected);
    expect(observed == expected);
}

template<int dim> void check_regional_errors_are_atomic()
{
    using namespace boost::ut;
    constexpr std::uint64_t bits = 0x7ff8000000000042ULL;
    const auto space = rift::test::make_space_with_one_phase_field<dim>({{"regional"}});
    const auto entry = space.layout().regional_entries().front().id;
    const auto unknown = rift::RegionalEntryId::from_index(entry.value() + 1U);
    auto store = rift::test::make_state_store(space);
    const auto base = store.snapshot(rift::StateSlot::accepted);
    auto trial = store.begin_trial_collective(base.stamp().snapshot).value();

    const auto exhausted = static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
    const std::array<std::uint64_t, 5> sentinel{exhausted, exhausted, exhausted, exhausted, exhausted};
    const SequenceOverride restore(sentinel);

    const auto rejected = trial.set_regional_value_collective(unknown, std::bit_cast<double>(bits));
    expect(!rejected.has_value());
    expect(rejected.error().code == rift::StateTransitionErrorCode::unknown_regional_entry);
    expect(rift::detail::StateStoreAccess::regional_cache_bits(trial, entry) == std::uint64_t{0});
    expect(rift::detail::StateStoreAccess::regional_backend_bits(trial, entry) == std::uint64_t{0});
    expect_sequences(sentinel);

    expect(trial.set_regional_value_collective(entry, std::bit_cast<double>(bits)).has_value());
    expect(rift::detail::StateStoreAccess::regional_cache_bits(trial, entry) == bits);
    expect(rift::detail::StateStoreAccess::regional_backend_bits(trial, entry) == bits);
    expect_sequences(sentinel);

    trial.abandon();
    const auto inactive = trial.set_regional_value_collective(unknown, 1.0);
    expect(!inactive.has_value());
    expect(inactive.error().code == rift::StateTransitionErrorCode::inactive_transaction);
    expect_sequences(sentinel);
}

template<int dim> void check_expired_precedes_unknown()
{
    using namespace boost::ut;
    const auto space = rift::test::make_space_with_one_phase_field<dim>({{"regional"}});
    auto store = std::optional<rift::StateStore>{rift::test::make_state_store(space)};
    const auto base = store->snapshot(rift::StateSlot::accepted);
    auto trial = store->begin_trial_collective(base.stamp().snapshot).value();
    store.reset();
    const auto expired = trial.set_regional_value_collective(rift::RegionalEntryId::from_index(100), 2.0);
    expect(!expired.has_value());
    expect(expired.error().code == rift::StateTransitionErrorCode::expired_store);
    trial.abandon();
    const auto expired_and_inactive = trial.set_regional_value_collective(
        space.layout().regional_entries().front().id, std::bit_cast<double>(std::uint64_t{0x3ff0000000000001ULL}));
    expect(!expired_and_inactive.has_value());
    expect(expired_and_inactive.error().code == rift::StateTransitionErrorCode::expired_store);
}

template<int dim> void check_failed_seal_preserves_regional_storage()
{
    using namespace boost::ut;
    constexpr std::uint64_t backend_bits = 0x7ff8000000000042ULL;
    const auto space = rift::test::make_space_with_one_phase_field<dim>({{"regional"}});
    const auto entry = space.layout().regional_entries().front().id;
    auto store = rift::test::make_state_store(space);
    const auto base = store.snapshot(rift::StateSlot::accepted);
    auto trial = store.begin_trial_collective(base.stamp().snapshot).value();
    rift::detail::StateStoreAccess::set_regional_backend_bits_for_test(trial, entry, backend_bits);

    const auto exhausted = static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
    const std::array<std::uint64_t, 5> exhausted_snapshot{17U, 18U, exhausted, 20U, 21U};
    {
        const SequenceOverride restore(exhausted_snapshot);
        const auto failed = trial.seal_collective();
        expect(!failed.has_value());
        expect(failed.error().code == rift::StateTransitionErrorCode::identity_exhausted);
        expect(trial.active());
        expect(rift::detail::StateStoreAccess::regional_cache_bits(trial, entry) == std::uint64_t{0});
        expect(rift::detail::StateStoreAccess::regional_backend_bits(trial, entry) == backend_bits);
        expect_sequences(exhausted_snapshot);
    }

    const auto corrected = trial.seal_collective();
    expect(corrected.has_value());
    expect(rift::detail::StateStoreAccess::regional_cache_bits(*corrected, entry) == backend_bits);
    expect(rift::detail::StateStoreAccess::regional_backend_bits(*corrected, entry) == backend_bits);
}

} // namespace rift_test::state_regional_errors_00

namespace rift_test::state_regional_errors_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "regional setter errors are atomic and reserve no identity component"_test = [] {
        check_regional_errors_are_atomic<2>();
        check_regional_errors_are_atomic<3>();
        check_expired_precedes_unknown<2>();
        check_expired_precedes_unknown<3>();
        check_failed_seal_preserves_regional_storage<2>();
        check_failed_seal_preserves_regional_storage<3>();
    };
}

} // namespace rift_test::state_regional_errors_00
