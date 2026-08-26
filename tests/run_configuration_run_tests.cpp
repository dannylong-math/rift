#include "mpi/run_configuration_disjoint_ids_00.hpp"
#include "mpi/run_configuration_id_agreement_00.hpp"
#include "mpi/run_configuration_reject_intercommunicator_00.hpp"
#include "rift/run_configuration.hpp"
#include "run_configuration_allocation_failures_00.hpp"
#include "run_configuration_cleanup_failures_00.hpp"
#include "run_configuration_graph_lifetime_00.hpp"
#include "run_configuration_id_allocator_00.hpp"
#include "run_configuration_lifecycle_failures_00.hpp"
#include "run_configuration_mpi_failures_00.hpp"
#include "run_configuration_origin_00.hpp"
#include "run_configuration_reject_finalized_00.hpp"
#include "run_configuration_reject_null_00.hpp"
#include "run_configuration_reject_uninitialized_00.hpp"
#include "test_runner_support.hpp"

#include <array>
#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <optional>

namespace {

enum class TestMode : std::uint8_t { serial, serial_reverse, uninitialized, finalized, mpi_2 };

constexpr auto serial_registrations = std::array{
    &rift_test::run_configuration_allocation_failures_00::register_tests,
    &rift_test::run_configuration_cleanup_failures_00::register_tests,
    &rift_test::run_configuration_graph_lifetime_00::register_tests,
    &rift_test::run_configuration_lifecycle_failures_00::register_tests,
    &rift_test::run_configuration_mpi_failures_00::register_tests,
    &rift_test::run_configuration_origin_00::register_tests,
    &rift_test::run_configuration_reject_null_00::register_tests,
};

template<TestMode mode> int run_subject_suite()
{
    [[maybe_unused]] boost::ut::suite<"Run configuration"> const subject_suite = [] {
        if constexpr (mode == TestMode::serial || mode == TestMode::serial_reverse) {
            rift_test::register_all<mode == TestMode::serial_reverse>(serial_registrations);
        }
        else if constexpr (mode == TestMode::uninitialized) {
            rift_test::run_configuration_id_allocator_00::register_tests();
            rift_test::run_configuration_reject_uninitialized_00::register_tests();
        }
        else if constexpr (mode == TestMode::finalized) {
            rift_test::run_configuration_reject_finalized_00::register_tests();
        }
        else if constexpr (mode == TestMode::mpi_2) {
            rift_test::mpi::run_configuration_disjoint_ids_00::register_tests();
            rift_test::mpi::run_configuration_id_agreement_00::register_tests();
            rift_test::mpi::run_configuration_reject_intercommunicator_00::register_tests();
        }
    };
    return static_cast<int>(boost::ut::cfg<>.run());
}

template<TestMode mode> int run_initialized(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize const mpi(argc, argv, 1);
    return run_subject_suite<mode>();
}

int run_finalized(int argc, char** argv)
{
    MPI_Init(&argc, &argv);
    std::optional<rift::RunConfiguration> live_run;
    live_run.emplace(rift::RunConfiguration::create(MPI_COMM_SELF).value());
    MPI_Finalize();
    const auto result = run_subject_suite<TestMode::finalized>();
    live_run.reset();
    return result;
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
    if (mode == "uninitialized") {
        return run_subject_suite<TestMode::uninitialized>();
    }
    if (mode == "finalized") {
        return run_finalized(argc, argv);
    }
    if (mode == "mpi-2") {
        return run_initialized<TestMode::mpi_2>(argc, argv);
    }
    return rift_test::invalid_test_mode("run_configuration_run_tests", mode);
}
