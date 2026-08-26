#pragma once

#include "../src/mesh_snapshot_internal.hpp"
#include "../src/run_configuration_internal.hpp"
#include "mesh_snapshot_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <memory>
#include <mpi.h>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <new>
#include <rift/mesh_snapshot.hpp>

namespace rift_test::mesh_snapshot_communicator_ownership_failures_00 {

inline int fail_duplicate([[maybe_unused]] MPI_Comm source, [[maybe_unused]] MPI_Comm* duplicate)
{
    return MPI_ERR_OTHER;
}

inline int borrow_duplicate(const MPI_Comm source, MPI_Comm* duplicate)
{
    *duplicate = source;
    return MPI_SUCCESS;
}

inline std::shared_ptr<const rift::detail::MeshCommunicatorControl>
fail_control_allocation([[maybe_unused]] MPI_Comm communicator, [[maybe_unused]] rift::detail::MpiAbort abort)
{
    throw std::bad_alloc{};
}

} // namespace rift_test::mesh_snapshot_communicator_ownership_failures_00

namespace rift_test::mesh_snapshot_communicator_ownership_failures_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "communicator duplication failure is fatal"_test = [] {
        const auto run = mesh_snapshot_test::make_run();
        try {
            static_cast<void>(rift::detail::own_mesh_communicator_with_operations(
                rift::detail::RunConfigurationAccess::control(run), MPI_COMM_SELF, fail_duplicate,
                rift::detail::allocate_mesh_communicator_control));
            expect(false);
        }
        catch (const mesh_snapshot_test::FatalMpiFailure& failure) {
            expect(failure.status == MPI_ERR_OTHER);
        }
    };

    "post-duplication control allocation failure is immediately fatal"_test = [] {
        const auto run = mesh_snapshot_test::make_run();
        try {
            static_cast<void>(rift::detail::own_mesh_communicator_with_operations(
                rift::detail::RunConfigurationAccess::control(run), MPI_COMM_SELF, borrow_duplicate,
                fail_control_allocation));
            expect(false);
        }
        catch (const mesh_snapshot_test::FatalMpiFailure& failure) {
            expect(failure.status == MPI_ERR_NO_MEM);
        }
    };

    "successful ownership exposes and releases the duplicated communicator"_test = [] {
        const auto run = mesh_snapshot_test::make_run();
        const auto retained =
            rift::detail::own_mesh_communicator(rift::detail::RunConfigurationAccess::control(run), MPI_COMM_SELF);
        int relationship = MPI_UNEQUAL;
        expect(retained.control->communicator() == retained.communicator);
        expect(MPI_Comm_compare(retained.communicator, MPI_COMM_SELF, &relationship) == MPI_SUCCESS);
        expect(relationship == MPI_CONGRUENT);
    };
}

} // namespace rift_test::mesh_snapshot_communicator_ownership_failures_00
