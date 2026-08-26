#include "mpi/space_registry_adaptive_closure_00.hpp"
#include "mpi/space_registry_begin_fatal_00.hpp"
#include "mpi/space_registry_caller_communicator_lifetime_00.hpp"
#include "mpi/space_registry_cascading_closure_00.hpp"
#include "mpi/space_registry_disjoint_multicomponent_00.hpp"
#include "mpi/space_registry_distributed_agreement_00.hpp"
#include "mpi/space_registry_finalize_fatal_00.hpp"
#include "mpi/space_registry_foreign_draft_transaction_00.hpp"
#include "space_registry_adaptive_closure_00.hpp"
#include "space_registry_cascading_closure_00.hpp"
#include "space_registry_collective_failures_00.hpp"
#include "space_registry_detail_framing_00.hpp"
#include "space_registry_diagnostic_merge_00.hpp"
#include "space_registry_disjoint_phases_00.hpp"
#include "space_registry_epoch_exhaustion_00.hpp"
#include "space_registry_epoch_provenance_00.hpp"
#include "space_registry_fatal_00.hpp"
#include "space_registry_groups_00.hpp"
#include "space_registry_inactive_cell_00.hpp"
#include "space_registry_level_set_background_00.hpp"
#include "space_registry_non_dominating_nothing_00.hpp"
#include "space_registry_operation_failures_00.hpp"
#include "space_registry_phase_support_00.hpp"
#include "space_registry_provenance_errors_00.hpp"
#include "space_registry_rebuild_epoch_00.hpp"
#include "space_registry_regions_00.hpp"
#include "space_registry_reject_duplicate_group_00.hpp"
#include "space_registry_reject_invalid_field_00.hpp"
#include "space_registry_reject_invalid_level_set_00.hpp"
#include "space_registry_reject_regions_00.hpp"
#include "space_registry_reject_unknown_cell_00.hpp"
#include "space_registry_reject_unknown_phase_00.hpp"
#include "space_registry_support_cardinality_00.hpp"
#include "space_registry_support_envelope_00.hpp"
#include "space_registry_uniform_degree_00.hpp"
#include "test_runner_support.hpp"

#include <array>
#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>

namespace {

enum class TestMode : std::uint8_t { serial, serial_reverse, mpi_2, mpi_3, fatal_begin, fatal_finalize };

constexpr auto serial_registrations = std::array{
    &rift_test::space_registry_adaptive_closure_00::register_tests,
    &rift_test::space_registry_cascading_closure_00::register_tests,
    &rift_test::space_registry_collective_failures_00::register_tests,
    &rift_test::space_registry_detail_framing_00::register_tests,
    &rift_test::space_registry_diagnostic_merge_00::register_tests,
    &rift_test::space_registry_disjoint_phases_00::register_tests,
    &rift_test::space_registry_epoch_exhaustion_00::register_tests,
    &rift_test::space_registry_epoch_provenance_00::register_tests,
    &rift_test::space_registry_fatal_00::register_tests,
    &rift_test::space_registry_groups_00::register_tests,
    &rift_test::space_registry_inactive_cell_00::register_tests,
    &rift_test::space_registry_level_set_background_00::register_tests,
    &rift_test::space_registry_non_dominating_nothing_00::register_tests,
    &rift_test::space_registry_operation_failures_00::register_tests,
    &rift_test::space_registry_phase_support_00::register_tests,
    &rift_test::space_registry_provenance_errors_00::register_tests,
    &rift_test::space_registry_rebuild_epoch_00::register_tests,
    &rift_test::space_registry_regions_00::register_tests,
    &rift_test::space_registry_reject_duplicate_group_00::register_tests,
    &rift_test::space_registry_reject_invalid_field_00::register_tests,
    &rift_test::space_registry_reject_invalid_level_set_00::register_tests,
    &rift_test::space_registry_reject_regions_00::register_tests,
    &rift_test::space_registry_reject_unknown_cell_00::register_tests,
    &rift_test::space_registry_reject_unknown_phase_00::register_tests,
    &rift_test::space_registry_support_cardinality_00::register_tests,
    &rift_test::space_registry_support_envelope_00::register_tests,
    &rift_test::space_registry_uniform_degree_00::register_tests,
};

void register_mpi_common_tests()
{
    rift_test::mpi::space_registry_adaptive_closure_00::register_tests();
    rift_test::mpi::space_registry_distributed_agreement_00::register_tests();
}

template<TestMode mode> int run_subject_suite()
{
    [[maybe_unused]] boost::ut::suite<"Space registry"> const subject_suite = [] {
        if constexpr (mode == TestMode::serial || mode == TestMode::serial_reverse) {
            rift_test::register_all<mode == TestMode::serial_reverse>(serial_registrations);
        }
        else if constexpr (mode == TestMode::mpi_2) {
            register_mpi_common_tests();
            rift_test::mpi::space_registry_caller_communicator_lifetime_00::register_tests();
            rift_test::mpi::space_registry_cascading_closure_00::register_tests();
            rift_test::mpi::space_registry_disjoint_multicomponent_00::register_tests();
            rift_test::mpi::space_registry_foreign_draft_transaction_00::register_tests();
        }
        else if constexpr (mode == TestMode::mpi_3) {
            register_mpi_common_tests();
        }
        else if constexpr (mode == TestMode::fatal_begin) {
            rift_test::mpi::space_registry_begin_fatal_00::register_tests();
        }
        else if constexpr (mode == TestMode::fatal_finalize) {
            rift_test::mpi::space_registry_finalize_fatal_00::register_tests();
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
    if (mode == "fatal-begin") {
        return run_initialized<TestMode::fatal_begin>(argc, argv);
    }
    if (mode == "fatal-finalize") {
        return run_initialized<TestMode::fatal_finalize>(argc, argv);
    }
    return rift_test::invalid_test_mode("space_registry_run_tests", mode);
}
