#pragma once

#include "../src/mesh_snapshot_internal.hpp"
#include "mesh_snapshot_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/mesh_snapshot.hpp>

namespace rift_test::mesh_snapshot_lifecycle_00 {

inline int report_not_initialized(int* value)
{
    *value = 0;
    return MPI_SUCCESS;
}

inline int report_finalized(int* value)
{
    *value = 1;
    return MPI_SUCCESS;
}

inline int fail_query([[maybe_unused]] int* value) { return MPI_ERR_OTHER; }

inline int fail_collective([[maybe_unused]] int local, [[maybe_unused]] int& agreed,
                           [[maybe_unused]] MPI_Comm communicator)
{
    return MPI_ERR_OTHER;
}

} // namespace rift_test::mesh_snapshot_lifecycle_00

namespace rift_test::mesh_snapshot_lifecycle_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "logical lifecycle and null values are recoverable"_test = [] {
        const auto run = mesh_snapshot_test::make_run();
        auto operations = mesh_snapshot_test::operations();
        operations.initialized = report_not_initialized;
        const auto uninitialized =
            rift::detail::retain_mesh_snapshot_run_context_with_operations(run, true, operations);
        expect(uninitialized.error().code == rift::MeshSnapshotErrorCode::mpi_not_initialized);

        operations = mesh_snapshot_test::operations();
        operations.finalized = report_finalized;
        const auto finalized = rift::detail::retain_mesh_snapshot_run_context_with_operations(run, true, operations);
        expect(finalized.error().code == rift::MeshSnapshotErrorCode::mpi_finalized);

        operations = mesh_snapshot_test::operations();
        const auto missing = rift::detail::retain_mesh_snapshot_run_context_with_operations(run, false, operations);
        expect(missing.error().code == rift::MeshSnapshotErrorCode::null_triangulation);
    };

    "MPI query and agreement failures are fatal"_test = [] {
        const auto run = mesh_snapshot_test::make_run();
        for (const int failure_case : {0, 1, 2}) {
            auto operations = mesh_snapshot_test::operations();
            if (failure_case == 0) {
                operations.initialized = fail_query;
            }
            else if (failure_case == 1) {
                operations.finalized = fail_query;
            }
            else {
                operations.collective_minimum = fail_collective;
            }
            try {
                [[maybe_unused]] const auto unexpected =
                    rift::detail::retain_mesh_snapshot_run_context_with_operations(run, true, operations);
                expect(false);
            }
            catch (const mesh_snapshot_test::FatalMpiFailure& failure) {
                expect(failure.status == MPI_ERR_OTHER);
            }
        }
    };
}

} // namespace rift_test::mesh_snapshot_lifecycle_00
