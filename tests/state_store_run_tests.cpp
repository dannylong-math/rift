#include "mpi/state_begin_fatal_00.hpp"
#include "mpi/state_collective_agreement_00.hpp"
#include "mpi/state_factory_dependency_fatal_00.hpp"
#include "mpi/state_factory_fatal_00.hpp"
#include "mpi/state_factory_layout_context_00.hpp"
#include "mpi/state_global_ids_00.hpp"
#include "mpi/state_publish_fatal_00.hpp"
#include "mpi/state_retention_agreement_00.hpp"
#include "state_identity_allocator_00.hpp"
#include "state_identity_protocol_exhaustion_00.hpp"
#include "state_mpi_status_00.hpp"
#include "state_store_collective_api_00.hpp"
#include "state_store_discard_00.hpp"
#include "state_store_discard_published_00.hpp"
#include "state_store_discard_unknown_00.hpp"
#include "state_store_initial_00.hpp"
#include "state_store_lifetime_00.hpp"
#include "state_store_model_00.hpp"
#include "state_store_move_transaction_00.hpp"
#include "state_store_previous_missing_00.hpp"
#include "state_store_private_lineage_00.hpp"
#include "state_store_reject_republish_00.hpp"
#include "state_store_reject_unknown_snapshot_00.hpp"
#include "state_store_retention_00.hpp"
#include "state_store_retention_errors_00.hpp"
#include "state_store_retention_eviction_00.hpp"
#include "state_store_retention_exhaustion_00.hpp"
#include "state_store_retention_identity_00.hpp"
#include "state_store_retention_lifetime_00.hpp"
#include "state_store_retention_model_00.hpp"
#include "state_store_retention_publish_00.hpp"
#include "state_store_space_epoch_00.hpp"
#include "state_store_stale_sibling_00.hpp"
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
    fatal_factory,
    fatal_factory_dependency,
    fatal_begin,
    fatal_publish
};

constexpr auto serial_registrations = std::array{
    &rift_test::state_identity_allocator_00::register_tests,
    &rift_test::state_identity_protocol_exhaustion_00::register_tests,
    &rift_test::state_mpi_status_00::register_tests,
    &rift_test::state_store_collective_api_00::register_tests,
    &rift_test::state_store_discard_00::register_tests,
    &rift_test::state_store_discard_published_00::register_tests,
    &rift_test::state_store_discard_unknown_00::register_tests,
    &rift_test::state_store_initial_00::register_tests,
    &rift_test::state_store_lifetime_00::register_tests,
    &rift_test::state_store_model_00::register_tests,
    &rift_test::state_store_move_transaction_00::register_tests,
    &rift_test::state_store_previous_missing_00::register_tests,
    &rift_test::state_store_private_lineage_00::register_tests,
    &rift_test::state_store_reject_republish_00::register_tests,
    &rift_test::state_store_reject_unknown_snapshot_00::register_tests,
    &rift_test::state_store_retention_00::register_tests,
    &rift_test::state_store_retention_errors_00::register_tests,
    &rift_test::state_store_retention_eviction_00::register_tests,
    &rift_test::state_store_retention_exhaustion_00::register_tests,
    &rift_test::state_store_retention_identity_00::register_tests,
    &rift_test::state_store_retention_lifetime_00::register_tests,
    &rift_test::state_store_retention_model_00::register_tests,
    &rift_test::state_store_retention_publish_00::register_tests,
    &rift_test::state_store_space_epoch_00::register_tests,
    &rift_test::state_store_stale_sibling_00::register_tests,
};

void register_mpi_common_tests()
{
    rift_test::mpi::state_collective_agreement_00::register_tests();
    rift_test::mpi::state_factory_layout_context_00::register_tests();
    rift_test::mpi::state_retention_agreement_00::register_tests();
}

template<TestMode mode> int run_subject_suite()
{
    [[maybe_unused]] boost::ut::suite<"State store"> const subject_suite = [] {
        if constexpr (mode == TestMode::serial || mode == TestMode::serial_reverse) {
            rift_test::register_all<mode == TestMode::serial_reverse>(serial_registrations);
        }
        else if constexpr (mode == TestMode::mpi_2) {
            register_mpi_common_tests();
            rift_test::mpi::state_global_ids_00::register_tests();
        }
        else if constexpr (mode == TestMode::mpi_3) {
            register_mpi_common_tests();
        }
        else if constexpr (mode == TestMode::fatal_factory) {
            rift_test::mpi::state_factory_fatal_00::register_tests();
        }
        else if constexpr (mode == TestMode::fatal_factory_dependency) {
            rift_test::mpi::state_factory_dependency_fatal_00::register_tests();
        }
        else if constexpr (mode == TestMode::fatal_begin) {
            rift_test::mpi::state_begin_fatal_00::register_tests();
        }
        else if constexpr (mode == TestMode::fatal_publish) {
            rift_test::mpi::state_publish_fatal_00::register_tests();
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
    if (mode == "fatal-state-factory") {
        return run_initialized<TestMode::fatal_factory>(argc, argv);
    }
    if (mode == "fatal-state-factory-dependency") {
        return run_initialized<TestMode::fatal_factory_dependency>(argc, argv);
    }
    if (mode == "fatal-state-begin") {
        return run_initialized<TestMode::fatal_begin>(argc, argv);
    }
    if (mode == "fatal-state-publish") {
        return run_initialized<TestMode::fatal_publish>(argc, argv);
    }
    return rift_test::invalid_test_mode("state_store_run_tests", mode);
}
