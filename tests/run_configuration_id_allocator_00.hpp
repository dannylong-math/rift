#pragma once

#include "../src/run_configuration_internal.hpp"

#include <boost/ut.hpp>
#include <cstdint>
#include <limits>
#include <memory>
#include <rift/run_configuration.hpp>

namespace rift_test::run_configuration_id_allocator_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "the private ID allocator saturates instead of wrapping"_test = [] {
        rift::detail::MonotonicIdAllocator allocator(std::numeric_limits<std::uint64_t>::max());

        expect(allocator.reserve_local_candidate() == std::numeric_limits<std::uint64_t>::max());
    };

    "the per-origin run sequence reserves its maximum representation for exhaustion"_test = [] {
        rift::detail::RunSequenceAllocator allocator(std::numeric_limits<std::uint32_t>::max() - 1ULL);

        expect(allocator.reserve().value() == std::numeric_limits<std::uint32_t>::max() - 1U);
        expect(allocator.reserve().error().code == rift::RunConfigurationErrorCode::id_space_exhausted);
    };

    "collective agreement advances a lagging local allocator only when needed"_test = [] {
        rift::detail::MonotonicIdAllocator allocator(3);

        expect(allocator.reserve_local_candidate() == 3_u);
        allocator.advance_beyond(8);
        expect(allocator.reserve_local_candidate() == 9_u);
        allocator.advance_beyond(4);
        expect(allocator.reserve_local_candidate() == 10_u);
    };

    "private control cleanup before MPI initialization does not free a communicator"_test = [] {
        const auto control = std::make_shared<rift::detail::RunConfigurationControl>(
            MPI_COMM_NULL, rift::RunConfigurationId::from_index(7));

        expect(control->communicator() == MPI_COMM_NULL);
    };
}

} // namespace rift_test::run_configuration_id_allocator_00
