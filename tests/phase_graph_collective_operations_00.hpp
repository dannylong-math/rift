#pragma once

#include "../src/phase_graph_internal.hpp"

#include <boost/ut.hpp>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mpi.h>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rift_test::phase_graph_collective_operations_00 {

using namespace std::string_view_literals;

struct FatalMpiFailure {
    int status;
};

struct Script {
    int communicator_size = 2;
    int failing_call = -1;
    int call = 0;
    bool different_remote_bytes = false;
    std::optional<std::uint64_t> remote_length;
    bool any = false;
};

inline Script& script()
{
    static Script value;
    return value;
}

inline int scripted_status() { return script().call++ == script().failing_call ? MPI_ERR_OTHER : MPI_SUCCESS; }

inline int scripted_size([[maybe_unused]] MPI_Comm communicator, int* size)
{
    *size = script().communicator_size;
    return scripted_status();
}

inline int scripted_gather_byte_counts(const std::uint64_t local, const std::span<std::uint64_t> lengths,
                                       [[maybe_unused]] MPI_Comm communicator)
{
    lengths.front() = local;
    lengths.back() = script().remote_length.value_or(local);
    return scripted_status();
}

inline int scripted_gather_exact_bytes(const rift::detail::ExactByteGatherRequest& request,
                                       [[maybe_unused]] const MPI_Comm communicator)
{
    const auto counts = request.counts;
    const auto offsets = request.displacements;
    const auto remote_count = static_cast<std::size_t>(counts.back());
    const auto remote_offset = static_cast<std::size_t>(offsets.back());
    const auto output_size = remote_offset + remote_count;
    const auto output = request.gathered_bytes.first(output_size);
    std::memcpy(output.subspan(static_cast<std::size_t>(offsets.front())).data(), request.local_bytes.data(),
                request.local_bytes.size());
    std::memcpy(output.subspan(remote_offset).data(), request.local_bytes.data(), remote_count);
    if (script().different_remote_bytes && remote_count > 0U) {
        const auto remote = output.subspan(remote_offset, remote_count);
        remote.front() = remote.front() == 'x' ? 'y' : 'x';
    }
    return scripted_status();
}

inline int scripted_maximum(const std::uint64_t local, std::uint64_t& chosen, [[maybe_unused]] MPI_Comm communicator)
{
    chosen = script().any ? 1U : local;
    return scripted_status();
}

inline int throw_abort([[maybe_unused]] MPI_Comm communicator, const int status) { throw FatalMpiFailure{status}; }

inline std::vector<std::uint64_t> fail_uint64_allocation([[maybe_unused]] std::size_t size) { throw std::bad_alloc{}; }

inline std::vector<int> fail_int_allocation([[maybe_unused]] std::size_t size) { throw std::bad_alloc{}; }

inline std::string fail_byte_allocation([[maybe_unused]] std::size_t size) { throw std::bad_alloc{}; }

inline rift::detail::PhaseGraphCollectiveOperations scripted_operations()
{
    return {.communicator_size = scripted_size,
            .gather_byte_counts = scripted_gather_byte_counts,
            .gather_exact_bytes = scripted_gather_exact_bytes,
            .allocate_uint64_buffer = rift::detail::allocate_uint64_buffer,
            .allocate_int_buffer = rift::detail::allocate_int_buffer,
            .allocate_byte_buffer = rift::detail::allocate_byte_buffer,
            .collective_maximum = scripted_maximum,
            .abort = throw_abort};
}

template<class Function> void expect_fatal(const int expected_status, Function function)
{
    using namespace boost::ut;
    try {
        function();
        expect(false);
    }
    catch (const FatalMpiFailure& failure) {
        expect(failure.status == expected_status);
    }
}

} // namespace rift_test::phase_graph_collective_operations_00

namespace rift_test::phase_graph_collective_operations_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "exact byte agreement distinguishes equal and unequal payloads"_test = [] {
        script() = {};
        expect(rift::detail::collectively_equal_bytes(MPI_COMM_SELF, "exact\0bytes"sv, scripted_operations()));
        script() = {};
        script().different_remote_bytes = true;
        expect(!rift::detail::collectively_equal_bytes(MPI_COMM_SELF, "exact"sv, scripted_operations()));
    };

    "collective exception agreement returns the communicator disjunction"_test = [] {
        script() = {};
        expect(!rift::detail::collective_any(MPI_COMM_SELF, false, scripted_operations()));
        script() = {};
        script().any = true;
        expect(rift::detail::collective_any(MPI_COMM_SELF, false, scripted_operations()));
    };

    "every collective MPI status failure enters the fatal path"_test = [] {
        for (int failing_call = 0; failing_call < 3; ++failing_call) {
            script() = {};
            script().failing_call = failing_call;
            expect_fatal(MPI_ERR_OTHER, [] {
                static_cast<void>(rift::detail::collectively_equal_bytes(MPI_COMM_SELF, "x", scripted_operations()));
            });
        }
        script() = {};
        script().failing_call = 0;
        expect_fatal(MPI_ERR_OTHER, [] {
            static_cast<void>(rift::detail::collective_any(MPI_COMM_SELF, false, scripted_operations()));
        });
    };

    "unrepresentable collective byte counts enter the fatal path"_test = [] {
        script() = {};
        script().communicator_size = 0;
        expect_fatal(MPI_ERR_COUNT, [] {
            static_cast<void>(rift::detail::collectively_equal_bytes(MPI_COMM_SELF, "x", scripted_operations()));
        });

        script() = {};
        const std::string_view too_large{"x", static_cast<std::size_t>(std::numeric_limits<int>::max()) + 1U};
        expect_fatal(MPI_ERR_COUNT, [&too_large] {
            static_cast<void>(rift::detail::collectively_equal_bytes(MPI_COMM_SELF, too_large, scripted_operations()));
        });

        script() = {};
        script().remote_length = static_cast<std::uint64_t>(std::numeric_limits<int>::max()) + 1U;
        expect_fatal(MPI_ERR_COUNT, [] {
            static_cast<void>(rift::detail::collectively_equal_bytes(MPI_COMM_SELF, "x", scripted_operations()));
        });

        script() = {};
        script().remote_length = static_cast<std::uint64_t>(std::numeric_limits<int>::max());
        expect_fatal(MPI_ERR_COUNT, [] {
            static_cast<void>(rift::detail::collectively_equal_bytes(MPI_COMM_SELF, "x", scripted_operations()));
        });
    };

    "collective storage allocation failures enter the fatal path"_test = [] {
        auto operations = scripted_operations();
        operations.allocate_uint64_buffer = fail_uint64_allocation;
        script() = {};
        expect_fatal(MPI_ERR_NO_MEM, [&operations] {
            static_cast<void>(rift::detail::collectively_equal_bytes(MPI_COMM_SELF, "x", operations));
        });

        operations = scripted_operations();
        operations.allocate_int_buffer = fail_int_allocation;
        script() = {};
        expect_fatal(MPI_ERR_NO_MEM, [&operations] {
            static_cast<void>(rift::detail::collectively_equal_bytes(MPI_COMM_SELF, "x", operations));
        });

        operations = scripted_operations();
        operations.allocate_byte_buffer = fail_byte_allocation;
        script() = {};
        expect_fatal(MPI_ERR_NO_MEM, [&operations] {
            static_cast<void>(rift::detail::collectively_equal_bytes(MPI_COMM_SELF, "x", operations));
        });
    };
}

} // namespace rift_test::phase_graph_collective_operations_00
