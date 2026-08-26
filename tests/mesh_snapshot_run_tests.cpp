#include "mesh_snapshot_allocation_failures_00.hpp"
#include "mesh_snapshot_cleanup_failures_00.hpp"
#include "mesh_snapshot_communicator_00.hpp"
#include "mesh_snapshot_communicator_ownership_failures_00.hpp"
#include "mesh_snapshot_distinct_id_00.hpp"
#include "mesh_snapshot_fatal_cleanup_00.hpp"
#include "mesh_snapshot_id_allocator_00.hpp"
#include "mesh_snapshot_lifecycle_00.hpp"
#include "mesh_snapshot_mpi_failures_00.hpp"
#include "mesh_snapshot_null_00.hpp"
#include "mesh_snapshot_ownership_00.hpp"
#include "mesh_snapshot_preparation_00.hpp"
#include "mesh_snapshot_private_access_00.hpp"
#include "mesh_snapshot_run_lifetime_00.hpp"
#include "mesh_snapshot_world_members_00.hpp"
#include "mpi/mesh_snapshot_asymmetric_communicator_00.hpp"
#include "mpi/mesh_snapshot_asymmetric_null_00.hpp"
#include "mpi/mesh_snapshot_distributed_agreement_00.hpp"
#include "mpi/mesh_snapshot_distributed_input_errors_00.hpp"
#include "mpi/mesh_snapshot_global_ids_00.hpp"
#include "mpi/mesh_snapshot_reject_similar_00.hpp"
#include "mpi/mesh_snapshot_reject_unrelated_00.hpp"
#include "test_runner_support.hpp"

#include <array>
#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>

namespace {

enum class TestMode : std::uint8_t { serial, serial_reverse, mpi_2 };

constexpr auto serial_registrations = std::array{
    &rift_test::mesh_snapshot_allocation_failures_00::register_tests,
    &rift_test::mesh_snapshot_cleanup_failures_00::register_tests,
    &rift_test::mesh_snapshot_communicator_00::register_tests,
    &rift_test::mesh_snapshot_communicator_ownership_failures_00::register_tests,
    &rift_test::mesh_snapshot_distinct_id_00::register_tests,
    &rift_test::mesh_snapshot_fatal_cleanup_00::register_tests,
    &rift_test::mesh_snapshot_id_allocator_00::register_tests,
    &rift_test::mesh_snapshot_lifecycle_00::register_tests,
    &rift_test::mesh_snapshot_mpi_failures_00::register_tests,
    &rift_test::mesh_snapshot_null_00::register_tests,
    &rift_test::mesh_snapshot_ownership_00::register_tests,
    &rift_test::mesh_snapshot_preparation_00::register_tests,
    &rift_test::mesh_snapshot_private_access_00::register_tests,
    &rift_test::mesh_snapshot_run_lifetime_00::register_tests,
    &rift_test::mesh_snapshot_world_members_00::register_tests,
};

template<TestMode mode> int run_subject_suite()
{
    [[maybe_unused]] boost::ut::suite<"Mesh snapshot"> const subject_suite = [] {
        if constexpr (mode == TestMode::serial || mode == TestMode::serial_reverse) {
            rift_test::register_all<mode == TestMode::serial_reverse>(serial_registrations);
        }
        else if constexpr (mode == TestMode::mpi_2) {
            rift_test::mpi::mesh_snapshot_asymmetric_communicator_00::register_tests();
            rift_test::mpi::mesh_snapshot_asymmetric_null_00::register_tests();
            rift_test::mpi::mesh_snapshot_distributed_agreement_00::register_tests();
            rift_test::mpi::mesh_snapshot_distributed_input_errors_00::register_tests();
            rift_test::mpi::mesh_snapshot_global_ids_00::register_tests();
            rift_test::mpi::mesh_snapshot_reject_similar_00::register_tests();
            rift_test::mpi::mesh_snapshot_reject_unrelated_00::register_tests();
        }
    };
    return static_cast<int>(boost::ut::cfg<>.run());
}

template<TestMode mode> int run_initialized(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize const mpi(argc, argv, 1);
    return run_subject_suite<mode>();
}

} // namespace

int main(int argc, char** argv)
{
    const auto mode = rift_test::test_mode(argc, argv);
    if (mode == "serial") {
        return run_initialized<TestMode::serial>(argc, argv);
    }
    if (mode == "serial-reverse") {
        return run_initialized<TestMode::serial_reverse>(argc, argv);
    }
    if (mode == "mpi-2") {
        return run_initialized<TestMode::mpi_2>(argc, argv);
    }
    return rift_test::invalid_test_mode("mesh_snapshot_run_tests", mode);
}
