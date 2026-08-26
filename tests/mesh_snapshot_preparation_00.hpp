#pragma once

#include "../src/mesh_snapshot_internal.hpp"
#include "../src/run_configuration_internal.hpp"
#include "mesh_snapshot_test_support.hpp"

#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <limits>
#include <mpi.h>
#include <rift/mesh_snapshot.hpp>
#include <utility>

namespace rift_test::mesh_snapshot_preparation_00 {

inline rift::detail::MeshCommunicatorClassification& selected_classification()
{
    static auto value = rift::detail::MeshCommunicatorClassification::valid;
    return value;
}

inline int select_classification(const int local, int& agreed, [[maybe_unused]] MPI_Comm communicator)
{
    if (local == static_cast<int>(rift::detail::MeshCommunicatorClassification::valid)) {
        agreed = static_cast<int>(selected_classification());
    }
    else {
        agreed = local;
    }
    return MPI_SUCCESS;
}

inline int fail_minimum([[maybe_unused]] int local, [[maybe_unused]] int& agreed,
                        [[maybe_unused]] MPI_Comm communicator)
{
    return MPI_ERR_OTHER;
}

inline int& minimum_call_count()
{
    static int count = 0;
    return count;
}
inline int fail_second_minimum(const int local, int& agreed, [[maybe_unused]] MPI_Comm communicator)
{
    ++minimum_call_count();
    agreed = local;
    return minimum_call_count() == 2 ? MPI_ERR_OTHER : MPI_SUCCESS;
}

inline int throw_allocation([[maybe_unused]] int local, [[maybe_unused]] int& agreed,
                            [[maybe_unused]] MPI_Comm communicator)
{
    throw std::bad_alloc{};
}

} // namespace rift_test::mesh_snapshot_preparation_00

namespace rift_test::mesh_snapshot_preparation_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "agreed communicator classifications produce structured errors"_test = [] {
        const auto run = mesh_snapshot_test::make_run();
        auto operations = mesh_snapshot_test::operations();
        operations.collective_minimum = select_classification;
        rift::detail::MeshSequenceAllocator allocator;
        const auto control = rift::detail::RunConfigurationAccess::control(run);

        for (const auto [classification, code] :
             {std::pair{rift::detail::MeshCommunicatorClassification::intercommunicator,
                        rift::MeshSnapshotErrorCode::intercommunicator_not_supported},
              std::pair{rift::detail::MeshCommunicatorClassification::not_world_derived,
                        rift::MeshSnapshotErrorCode::communicator_not_world_derived},
              std::pair{rift::detail::MeshCommunicatorClassification::reordered,
                        rift::MeshSnapshotErrorCode::communicator_reordered},
              std::pair{rift::detail::MeshCommunicatorClassification::mismatch,
                        rift::MeshSnapshotErrorCode::communicator_mismatch}}) {
            selected_classification() = classification;
            const auto result = rift::detail::prepare_mesh_snapshot_with_operations(
                control, MPI_COMM_SELF, rift::detail::MeshStorageClassification::valid, operations, allocator);
            expect(result.error().code == code);
        }
    };

    "storage kind and empty-mesh classifications agree before ID allocation"_test = [] {
        const auto run = mesh_snapshot_test::make_run();
        const auto control = rift::detail::RunConfigurationAccess::control(run);
        auto operations = mesh_snapshot_test::operations();
        rift::detail::MeshSequenceAllocator exhausted(std::numeric_limits<std::uint32_t>::max());

        const auto unsupported = rift::detail::prepare_mesh_snapshot_with_operations(
            control, MPI_COMM_SELF, rift::detail::MeshStorageClassification::unsupported, operations, exhausted);
        expect(unsupported.error().code == rift::MeshSnapshotErrorCode::unsupported_triangulation_kind);

        const auto empty = rift::detail::prepare_mesh_snapshot_with_operations(
            control, MPI_COMM_SELF, rift::detail::MeshStorageClassification::empty_distributed, operations, exhausted);
        expect(empty.error().code == rift::MeshSnapshotErrorCode::empty_distributed_triangulation);
    };

    "classification-agreement failure is fatal"_test = [] {
        const auto run = mesh_snapshot_test::make_run();
        auto operations = mesh_snapshot_test::operations();
        operations.collective_minimum = fail_minimum;
        rift::detail::MeshSequenceAllocator allocator;
        try {
            [[maybe_unused]] const auto unexpected = rift::detail::prepare_mesh_snapshot_with_operations(
                rift::detail::RunConfigurationAccess::control(run), MPI_COMM_SELF,
                rift::detail::MeshStorageClassification::valid, operations, allocator);
            expect(false);
        }
        catch (const mesh_snapshot_test::FatalMpiFailure& failure) {
            expect(failure.status == MPI_ERR_OTHER);
        }

        operations = mesh_snapshot_test::operations();
        operations.collective_minimum = fail_second_minimum;
        minimum_call_count() = 0;
        try {
            [[maybe_unused]] const auto unexpected = rift::detail::prepare_mesh_snapshot_with_operations(
                rift::detail::RunConfigurationAccess::control(run), MPI_COMM_SELF,
                rift::detail::MeshStorageClassification::valid, operations, allocator);
            expect(false);
        }
        catch (const mesh_snapshot_test::FatalMpiFailure& failure) {
            expect(failure.status == MPI_ERR_OTHER);
        }
    };

    "preparation propagates exhaustion and treats local allocation as fatal"_test = [] {
        const auto run = mesh_snapshot_test::make_run();
        const auto control = rift::detail::RunConfigurationAccess::control(run);
        auto operations = mesh_snapshot_test::operations();
        rift::detail::MeshSequenceAllocator exhausted(std::numeric_limits<std::uint32_t>::max());
        const auto result = rift::detail::prepare_mesh_snapshot_with_operations(
            control, MPI_COMM_SELF, rift::detail::MeshStorageClassification::valid, operations, exhausted);
        expect(result.error().code == rift::MeshSnapshotErrorCode::id_space_exhausted);

        operations.collective_minimum = throw_allocation;
        rift::detail::MeshSequenceAllocator allocator;
        try {
            [[maybe_unused]] const auto unexpected = rift::detail::prepare_mesh_snapshot_with_operations(
                control, MPI_COMM_SELF, rift::detail::MeshStorageClassification::valid, operations, allocator);
            expect(false);
        }
        catch (const mesh_snapshot_test::FatalMpiFailure& failure) {
            expect(failure.status == MPI_ERR_NO_MEM);
        }
    };
}

} // namespace rift_test::mesh_snapshot_preparation_00
