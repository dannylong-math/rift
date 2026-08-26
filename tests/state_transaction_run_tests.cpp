#include "mpi/state_level_set_revision_00.hpp"
#include "mpi/state_regional_agreement_00.hpp"
#include "mpi/state_regional_staging_fatal_00.hpp"
#include "mpi/state_regional_sync_fatal_00.hpp"
#include "mpi/state_seal_fatal_00.hpp"
#include "state_transaction_abandon_00.hpp"
#include "state_transaction_inactive_regional_00.hpp"
#include "state_transaction_inactive_seal_00.hpp"
#include "state_transaction_isolation_00.hpp"
#include "state_transaction_level_set_00.hpp"
#include "state_transaction_move_00.hpp"
#include "state_transaction_publish_00.hpp"
#include "state_transaction_regional_00.hpp"
#include "state_transition_error_00.hpp"
#include "state_transition_no_id_consumption_00.hpp"
#include "test_runner_support.hpp"

#include <array>
#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>

namespace {

enum class TestMode : std::uint8_t {
    serial,
    serial_reverse,
    mpi_2,
    mpi_3,
    fatal_seal,
    fatal_regional_sync,
    fatal_regional_staging
};

constexpr auto serial_registrations = std::array{
    &rift_test::state_transaction_abandon_00::register_tests,
    &rift_test::state_transaction_inactive_regional_00::register_tests,
    &rift_test::state_transaction_inactive_seal_00::register_tests,
    &rift_test::state_transaction_isolation_00::register_tests,
    &rift_test::state_transaction_level_set_00::register_tests,
    &rift_test::state_transaction_move_00::register_tests,
    &rift_test::state_transaction_publish_00::register_tests,
    &rift_test::state_transaction_regional_00::register_tests,
    &rift_test::state_transition_error_00::register_tests,
    &rift_test::state_transition_no_id_consumption_00::register_tests,
};

void register_mpi_tests()
{
    rift_test::mpi::state_level_set_revision_00::register_tests();
    rift_test::mpi::state_regional_agreement_00::register_tests();
}

template<TestMode mode> int run_subject_suite()
{
    [[maybe_unused]] boost::ut::suite<"Mutable state transaction"> const subject_suite = [] {
        if constexpr (mode == TestMode::serial || mode == TestMode::serial_reverse) {
            rift_test::register_all<mode == TestMode::serial_reverse>(serial_registrations);
        }
        else if constexpr (mode == TestMode::mpi_2 || mode == TestMode::mpi_3) {
            register_mpi_tests();
        }
        else if constexpr (mode == TestMode::fatal_seal) {
            rift_test::mpi::state_seal_fatal_00::register_tests();
        }
        else if constexpr (mode == TestMode::fatal_regional_sync) {
            rift_test::mpi::state_regional_sync_fatal_00::register_tests();
        }
        else if constexpr (mode == TestMode::fatal_regional_staging) {
            rift_test::mpi::state_regional_staging_fatal_00::register_tests();
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
    if (mode == "mpi-3") {
        return run_initialized<TestMode::mpi_3>(argc, argv);
    }
    if (mode == "fatal-state-seal") {
        return run_initialized<TestMode::fatal_seal>(argc, argv);
    }
    if (mode == "fatal-state-regional-sync") {
        return run_initialized<TestMode::fatal_regional_sync>(argc, argv);
    }
    if (mode == "fatal-state-regional-staging") {
        return run_initialized<TestMode::fatal_regional_staging>(argc, argv);
    }
    return rift_test::invalid_test_mode("state_transaction_run_tests", mode);
}
