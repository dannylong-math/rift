#pragma once

#include <array>
#include <boost/ut.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <rift/discrete_state.hpp>

namespace rift_test::state_identity_allocator_00 {

inline void check_exhaustion_is_atomic()
{
    using namespace boost::ut;

    constexpr auto all_components =
        rift::detail::state_store_identity_mask | rift::detail::state_transaction_identity_mask |
        rift::detail::state_snapshot_identity_mask | rift::detail::state_epoch_identity_mask |
        rift::detail::state_level_set_identity_mask;
    for (std::size_t exhausted = 0; exhausted < 5; ++exhausted) {
        std::array<std::uint64_t, 5> next{3, 5, 7, 11, 13};
        next.at(exhausted) = std::numeric_limits<std::uint32_t>::max();
        const auto before = next;

        const auto reservation =
            rift::detail::reserve_state_sequence_values(next, {.origin_world_rank = 17, .mask = all_components});

        expect(!reservation.has_value());
        expect(reservation.error().code == rift::StateTransitionErrorCode::identity_exhausted);
        expect(next == before);
    }
}

inline void check_success_advances_only_selected_components()
{
    using namespace boost::ut;

    std::array<std::uint64_t, 5> next{2, 3, 5, 7, 11};
    constexpr auto selected = rift::detail::state_snapshot_identity_mask | rift::detail::state_level_set_identity_mask;
    const auto first =
        rift::detail::reserve_state_sequence_values(next, {.origin_world_rank = 19, .mask = selected}).value();
    const auto second =
        rift::detail::reserve_state_sequence_values(next, {.origin_world_rank = 19, .mask = selected}).value();
    const auto encode = [](const std::uint32_t origin, const std::uint32_t sequence) {
        return (static_cast<std::uint64_t>(origin) << 32U) | sequence;
    };

    expect(first.at(0) == 0U);
    expect(first.at(1) == 0U);
    expect(first.at(2) == encode(19, 5));
    expect(first.at(3) == 0U);
    expect(first.at(4) == encode(19, 11));
    expect(second.at(2) == encode(19, 6));
    expect(second.at(4) == encode(19, 12));
    expect((next == std::array<std::uint64_t, 5>{2, 3, 7, 7, 13}));
}

} // namespace rift_test::state_identity_allocator_00

namespace rift_test::state_identity_allocator_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "state identity bundle exhaustion never partially advances another sequence"_test = check_exhaustion_is_atomic;
    "state identity bundle success encodes an origin and advances only selected sequences"_test =
        check_success_advances_only_selected_components;
}

} // namespace rift_test::state_identity_allocator_00
