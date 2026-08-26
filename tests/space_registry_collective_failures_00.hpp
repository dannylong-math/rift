#pragma once

#include "../src/run_configuration_internal.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <boost/ut.hpp>
#include <cstddef>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <limits>
#include <memory>
#include <mpi.h>
#include <mpi_proto.h>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/run_configuration.hpp>
#include <span>
#include <string>
#include <string_view>

namespace rift_test::space_registry_collective_failures_00 {

struct FatalStatus {
    int value;
};

inline int throw_fatal([[maybe_unused]] MPI_Comm communicator, const int status) { throw FatalStatus{status}; }
inline int fail_rank([[maybe_unused]] MPI_Comm communicator, [[maybe_unused]] int* rank) { return MPI_ERR_OTHER; }
inline int rank_zero([[maybe_unused]] MPI_Comm communicator, int* rank)
{
    *rank = 0;
    return MPI_SUCCESS;
}
constexpr rift::detail::SpaceBroadcast fail_broadcast = []<class... Arguments>(Arguments... arguments) {
    static_cast<void>(sizeof...(arguments));
    return MPI_ERR_OTHER;
};
inline int fail_maximum([[maybe_unused]] int local, [[maybe_unused]] int& global,
                        [[maybe_unused]] MPI_Comm communicator)
{
    return MPI_ERR_OTHER;
}
inline int fail_size([[maybe_unused]] MPI_Comm communicator, [[maybe_unused]] int* size) { return MPI_ERR_OTHER; }
inline int one_rank([[maybe_unused]] MPI_Comm communicator, int* size)
{
    *size = 1;
    return MPI_SUCCESS;
}
inline int two_ranks([[maybe_unused]] MPI_Comm communicator, int* size)
{
    *size = 2;
    return MPI_SUCCESS;
}
inline int fail_sizes([[maybe_unused]] int local, [[maybe_unused]] std::span<int> sizes,
                      [[maybe_unused]] MPI_Comm communicator)
{
    return MPI_ERR_OTHER;
}
inline int negative_size([[maybe_unused]] int local, const std::span<int> sizes, [[maybe_unused]] MPI_Comm communicator)
{
    sizes.front() = -1;
    return MPI_SUCCESS;
}
inline int overflowing_sizes([[maybe_unused]] int local, const std::span<int> sizes,
                             [[maybe_unused]] MPI_Comm communicator)
{
    sizes.front() = std::numeric_limits<int>::max();
    sizes.back() = 1;
    return MPI_SUCCESS;
}
inline int one_byte_size([[maybe_unused]] int local, const std::span<int> sizes, [[maybe_unused]] MPI_Comm communicator)
{
    sizes.front() = 1;
    return MPI_SUCCESS;
}
inline int fail_bytes([[maybe_unused]] const rift::detail::SpacePayloadGatherRequest& request,
                      [[maybe_unused]] MPI_Comm communicator)
{
    return MPI_ERR_OTHER;
}
inline int one_byte_bytes(const rift::detail::SpacePayloadGatherRequest& request,
                          [[maybe_unused]] MPI_Comm communicator)
{
    request.bytes.front() = 'x';
    return MPI_SUCCESS;
}
inline std::string malformed_payload()
{
    return rift::detail::pack_space_strings(
        std::array{rift::detail::pack_space_strings(std::array<std::string, 3>{"1", "message", "extra"})});
}
inline int malformed_size([[maybe_unused]] int local, const std::span<int> sizes,
                          [[maybe_unused]] MPI_Comm communicator)
{
    sizes.front() = static_cast<int>(malformed_payload().size());
    return MPI_SUCCESS;
}
inline int malformed_bytes(const rift::detail::SpacePayloadGatherRequest& request,
                           [[maybe_unused]] MPI_Comm communicator)
{
    const auto payload = malformed_payload();
    std::ranges::copy(payload, request.bytes.begin());
    return MPI_SUCCESS;
}

template<class Operation> void expect_fatal(const int status, Operation operation)
{
    using namespace boost::ut;
    bool observed = false;
    try {
        operation();
    }
    catch (const FatalStatus& failure) {
        observed = true;
        expect(failure.value == status);
    }
    expect(observed);
}

} // namespace rift_test::space_registry_collective_failures_00

namespace rift_test::space_registry_collective_failures_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "space identity MPI failures are fatal"_test = [] {
        MPI_Comm owned = MPI_COMM_NULL;
        expect(MPI_Comm_dup(MPI_COMM_SELF, &owned) == MPI_SUCCESS);
        auto control = std::make_shared<const rift::detail::RunConfigurationControl>(
            owned, rift::RunConfigurationId::from_index(0), 0, throw_fatal);
        std::atomic<std::uint64_t> next{0};
        expect_fatal(MPI_ERR_OTHER, [&] {
            [[maybe_unused]] const auto result = rift::detail::reserve_space_identity(
                control, MPI_COMM_SELF, rift::MeshSnapshotId::from_index(0), next, fail_rank, MPI_Bcast);
        });
        expect_fatal(MPI_ERR_OTHER, [&] {
            [[maybe_unused]] const auto result = rift::detail::reserve_space_identity(
                control, MPI_COMM_SELF, rift::MeshSnapshotId::from_index(0), next, rank_zero, fail_broadcast);
        });
    };

    "space payload MPI and count failures are fatal"_test = [] {
        MPI_Comm owned = MPI_COMM_NULL;
        expect(MPI_Comm_dup(MPI_COMM_SELF, &owned) == MPI_SUCCESS);
        auto control = std::make_shared<const rift::detail::RunConfigurationControl>(
            owned, rift::RunConfigurationId::from_index(0), 0, throw_fatal);
        const auto invoke = [&](const rift::detail::SpaceCollectiveOperations operations,
                                const std::string_view payload = "x") {
            return rift::detail::all_gather_space_payloads(control, MPI_COMM_SELF, payload, operations);
        };
        expect_fatal(MPI_ERR_COUNT, [&] {
            [[maybe_unused]] const auto result =
                invoke({.communicator_size = one_rank, .gather_sizes = one_byte_size, .gather_bytes = one_byte_bytes},
                       std::string_view("x", static_cast<std::size_t>(std::numeric_limits<int>::max()) + 1));
        });
        expect_fatal(MPI_ERR_OTHER, [&] {
            [[maybe_unused]] const auto result =
                invoke({.communicator_size = fail_size, .gather_sizes = one_byte_size, .gather_bytes = one_byte_bytes});
        });
        expect_fatal(MPI_ERR_OTHER, [&] {
            [[maybe_unused]] const auto result =
                invoke({.communicator_size = one_rank, .gather_sizes = fail_sizes, .gather_bytes = one_byte_bytes});
        });
        expect_fatal(MPI_ERR_COUNT, [&] {
            [[maybe_unused]] const auto result =
                invoke({.communicator_size = one_rank, .gather_sizes = negative_size, .gather_bytes = one_byte_bytes});
        });
        expect_fatal(MPI_ERR_COUNT, [&] {
            [[maybe_unused]] const auto result = invoke(
                {.communicator_size = two_ranks, .gather_sizes = overflowing_sizes, .gather_bytes = one_byte_bytes});
        });
        expect_fatal(MPI_ERR_OTHER, [&] {
            [[maybe_unused]] const auto result =
                invoke({.communicator_size = one_rank, .gather_sizes = one_byte_size, .gather_bytes = fail_bytes});
        });
    };

    "malformed gathered diagnostics are fatal"_test = [] {
        MPI_Comm owned = MPI_COMM_NULL;
        expect(MPI_Comm_dup(MPI_COMM_SELF, &owned) == MPI_SUCCESS);
        auto control = std::make_shared<const rift::detail::RunConfigurationControl>(
            owned, rift::RunConfigurationId::from_index(0), 0, throw_fatal);
        expect_fatal(MPI_ERR_OTHER, [&] {
            [[maybe_unused]] const auto result = rift::detail::collective_space_errors(
                control, MPI_COMM_SELF, {},
                {.communicator_size = one_rank, .gather_sizes = malformed_size, .gather_bytes = malformed_bytes});
        });
        expect_fatal(MPI_ERR_OTHER, [&] {
            [[maybe_unused]] const auto result =
                rift::detail::collective_space_changed(control, MPI_COMM_SELF, 1, fail_maximum);
        });
    };
}

} // namespace rift_test::space_registry_collective_failures_00
