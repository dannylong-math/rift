#pragma once

#include "../src/mesh_snapshot_internal.hpp"
#include "../src/run_configuration_internal.hpp"
#include "mesh_snapshot_test_support.hpp"

#include <boost/ut.hpp>
#include <cstddef>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <span>

namespace rift_test::mesh_snapshot_world_members_00 {

inline int report_intracommunicator([[maybe_unused]] MPI_Comm communicator, int* value)
{
    *value = 0;
    return MPI_SUCCESS;
}

inline int report_three_members([[maybe_unused]] MPI_Comm communicator, int* size)
{
    *size = 3;
    return MPI_SUCCESS;
}

inline int provide_group([[maybe_unused]] MPI_Comm communicator, MPI_Group* group)
{
    *group = MPI_GROUP_EMPTY;
    return MPI_SUCCESS;
}

inline int translate_with_unsupported_middle([[maybe_unused]] MPI_Group source, const int count,
                                             const int* source_ranks, [[maybe_unused]] MPI_Group destination,
                                             int* destination_ranks)
{
    using namespace boost::ut;
    const auto source_members = std::span{source_ranks, static_cast<std::size_t>(count)};
    auto destination_members = std::span{destination_ranks, static_cast<std::size_t>(count)};
    expect(count == 3);
    expect(source_members.front() == 0);
    expect(source_members.subspan(1).front() == 1);
    expect(source_members.back() == 2);
    destination_members.front() = 4;
    destination_members.subspan(1).front() = MPI_UNDEFINED;
    destination_members.back() = 8;
    return MPI_SUCCESS;
}

inline int release_fake_group(MPI_Group* group)
{
    *group = MPI_GROUP_NULL;
    return MPI_SUCCESS;
}

inline int report_identical([[maybe_unused]] MPI_Comm first, [[maybe_unused]] MPI_Comm second, int* relationship)
{
    *relationship = MPI_IDENT;
    return MPI_SUCCESS;
}

} // namespace rift_test::mesh_snapshot_world_members_00

namespace rift_test::mesh_snapshot_world_members_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "every communicator member must belong to the current world"_test = [] {
        const auto run = mesh_snapshot_test::make_run();
        auto operations = mesh_snapshot_test::operations();
        operations.test_intercommunicator = report_intracommunicator;
        operations.communicator_size = report_three_members;
        operations.communicator_group = provide_group;
        operations.translate_ranks = translate_with_unsupported_middle;
        operations.free_group = release_fake_group;
        operations.compare = report_identical;

        const auto classification = rift::detail::classify_mesh_communicator(
            rift::detail::RunConfigurationAccess::control(run), MPI_COMM_SELF, operations);
        expect(classification == rift::detail::MeshCommunicatorClassification::not_world_derived);
    };
}

} // namespace rift_test::mesh_snapshot_world_members_00
