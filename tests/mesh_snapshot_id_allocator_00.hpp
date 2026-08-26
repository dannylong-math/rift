#pragma once

#include "../src/mesh_snapshot_internal.hpp"
#include "../src/run_configuration_internal.hpp"
#include "mesh_snapshot_test_support.hpp"

#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <limits>
#include <mpi.h>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <rift/mesh_snapshot.hpp>

namespace rift_test::mesh_snapshot_id_allocator_00 {

inline int report_nonroot([[maybe_unused]] MPI_Comm communicator, int* rank)
{
    *rank = 1;
    return MPI_SUCCESS;
}

inline int fail_rank([[maybe_unused]] MPI_Comm communicator, [[maybe_unused]] int* rank) { return MPI_ERR_OTHER; }

inline int provide_sequence(std::uint32_t& sequence, [[maybe_unused]] MPI_Comm communicator)
{
    sequence = 9;
    return MPI_SUCCESS;
}

inline int fail_broadcast([[maybe_unused]] std::uint32_t& sequence, [[maybe_unused]] MPI_Comm communicator)
{
    return MPI_ERR_OTHER;
}

} // namespace rift_test::mesh_snapshot_id_allocator_00

namespace rift_test::mesh_snapshot_id_allocator_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "mesh sequences stop before the reserved exhaustion sentinel"_test = [] {
        rift::detail::MeshSequenceAllocator allocator(std::numeric_limits<std::uint32_t>::max() - 1ULL);
        expect(allocator.reserve().value() == std::numeric_limits<std::uint32_t>::max() - 1U);
        expect(allocator.reserve().error().code == rift::MeshSnapshotErrorCode::id_space_exhausted);
    };

    "root and nonroot participants form the same origin-plus-sequence representation"_test = [] {
        constexpr std::uint64_t origin = 7;
        const auto run = mesh_snapshot_test::make_run(origin << 32U);
        auto operations = mesh_snapshot_test::operations();
        rift::detail::MeshSequenceAllocator root_allocator;
        const auto root = rift::detail::reserve_mesh_snapshot_id(rift::detail::RunConfigurationAccess::control(run),
                                                                 operations, root_allocator);
        expect(root->value() == (origin << 32U));

        operations.communicator_rank = report_nonroot;
        operations.broadcast_sequence = provide_sequence;
        rift::detail::MeshSequenceAllocator unused(std::numeric_limits<std::uint32_t>::max());
        const auto nonroot = rift::detail::reserve_mesh_snapshot_id(rift::detail::RunConfigurationAccess::control(run),
                                                                    operations, unused);
        expect(nonroot->value() == ((origin << 32U) | 9U));
    };

    "mesh ID exhaustion remains recoverable"_test = [] {
        const auto run = mesh_snapshot_test::make_run();
        auto operations = mesh_snapshot_test::operations();
        rift::detail::MeshSequenceAllocator exhausted(std::numeric_limits<std::uint32_t>::max());
        const auto result = rift::detail::reserve_mesh_snapshot_id(rift::detail::RunConfigurationAccess::control(run),
                                                                   operations, exhausted);
        expect(result.error().code == rift::MeshSnapshotErrorCode::id_space_exhausted);
    };

    "mesh ID MPI failures are fatal"_test = [] {
        const auto run = mesh_snapshot_test::make_run();
        for (const bool fail_rank_query : {true, false}) {
            auto operations = mesh_snapshot_test::operations();
            operations.communicator_rank = fail_rank_query ? fail_rank : MPI_Comm_rank;
            operations.broadcast_sequence = fail_rank_query ? mesh_snapshot_test::copy_sequence : fail_broadcast;
            rift::detail::MeshSequenceAllocator allocator;
            try {
                [[maybe_unused]] const auto unexpected = rift::detail::reserve_mesh_snapshot_id(
                    rift::detail::RunConfigurationAccess::control(run), operations, allocator);
                expect(false);
            }
            catch (const mesh_snapshot_test::FatalMpiFailure& failure) {
                expect(failure.status == MPI_ERR_OTHER);
            }
        }
    };
}

} // namespace rift_test::mesh_snapshot_id_allocator_00
