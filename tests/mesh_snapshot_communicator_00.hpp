#pragma once

#include "../src/mesh_snapshot_internal.hpp"
#include "../src/run_configuration_internal.hpp"
#include "mesh_snapshot_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>

namespace rift_test::mesh_snapshot_communicator_00 {

inline int report_intercommunicator([[maybe_unused]] MPI_Comm communicator, int* value)
{
    *value = 1;
    return MPI_SUCCESS;
}

inline int report_undefined([[maybe_unused]] MPI_Group source, [[maybe_unused]] const int count,
                            [[maybe_unused]] const int* source_ranks, [[maybe_unused]] MPI_Group destination,
                            int* destination_ranks)
{
    *destination_ranks = MPI_UNDEFINED;
    return MPI_SUCCESS;
}

inline int report_similar([[maybe_unused]] MPI_Comm first, [[maybe_unused]] MPI_Comm second, int* relationship)
{
    *relationship = MPI_SIMILAR;
    return MPI_SUCCESS;
}

inline int report_unequal([[maybe_unused]] MPI_Comm first, [[maybe_unused]] MPI_Comm second, int* relationship)
{
    *relationship = MPI_UNEQUAL;
    return MPI_SUCCESS;
}

} // namespace rift_test::mesh_snapshot_communicator_00

namespace rift_test::mesh_snapshot_communicator_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "communicator classification covers every logical relationship"_test = [] {
        const auto run = mesh_snapshot_test::make_run();
        const auto control = rift::detail::RunConfigurationAccess::control(run);

        auto operations = mesh_snapshot_test::operations();
        expect(rift::detail::classify_mesh_communicator(control, MPI_COMM_NULL, operations) ==
               rift::detail::MeshCommunicatorClassification::mismatch);
        expect(rift::detail::classify_mesh_communicator(control, MPI_COMM_SELF, operations) ==
               rift::detail::MeshCommunicatorClassification::valid);

        operations.test_intercommunicator = report_intercommunicator;
        expect(rift::detail::classify_mesh_communicator(control, MPI_COMM_SELF, operations) ==
               rift::detail::MeshCommunicatorClassification::intercommunicator);

        operations = mesh_snapshot_test::operations();
        operations.translate_ranks = report_undefined;
        expect(rift::detail::classify_mesh_communicator(control, MPI_COMM_SELF, operations) ==
               rift::detail::MeshCommunicatorClassification::not_world_derived);

        operations = mesh_snapshot_test::operations();
        operations.compare = report_similar;
        expect(rift::detail::classify_mesh_communicator(control, MPI_COMM_SELF, operations) ==
               rift::detail::MeshCommunicatorClassification::reordered);

        operations.compare = report_unequal;
        expect(rift::detail::classify_mesh_communicator(control, MPI_COMM_SELF, operations) ==
               rift::detail::MeshCommunicatorClassification::mismatch);
    };
}

} // namespace rift_test::mesh_snapshot_communicator_00
