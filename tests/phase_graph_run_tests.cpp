#include "mpi/phase_graph_callback_exception_00.hpp"
#include "mpi/phase_graph_callback_mismatch_00.hpp"
#include "mpi/phase_graph_input_mismatch_00.hpp"
#include "mpi/phase_graph_input_permutation_agreement_00.hpp"
#include "mpi/phase_graph_instance_id_agreement_00.hpp"
#include "mpi/phase_graph_three_rank_agreement_00.hpp"
#include "phase_graph_assignment_traits_00.hpp"
#include "phase_graph_callback_bad_alloc_00.hpp"
#include "phase_graph_canonical_tiebreak_00.hpp"
#include "phase_graph_collective_operations_00.hpp"
#include "phase_graph_compatibility_exception_00.hpp"
#include "phase_graph_compatibility_order_00.hpp"
#include "phase_graph_construct_00.hpp"
#include "phase_graph_construct_01.hpp"
#include "phase_graph_cycle_orientation_00.hpp"
#include "phase_graph_errors_00.hpp"
#include "phase_graph_input_allocation_failure_00.hpp"
#include "phase_graph_invalid_numeric_lookup_00.hpp"
#include "phase_graph_json_controls_00.hpp"
#include "phase_graph_lookup_00.hpp"
#include "phase_graph_lookup_01.hpp"
#include "phase_graph_lookup_missing_interface_00.hpp"
#include "phase_graph_orientation_00.hpp"
#include "phase_graph_permutations_00.hpp"
#include "phase_graph_post_agreement_allocation_failure_00.hpp"
#include "phase_graph_reference_invalid_id_00.hpp"
#include "phase_graph_reference_provenance_00.hpp"
#include "phase_graph_reject_id_exhaustion_00.hpp"
#include "phase_graph_serialize_00.hpp"
#include "phase_graph_serialize_escapes_00.hpp"
#include "phase_graph_serialize_multiple_interfaces_00.hpp"
#include "phase_graph_utf8_fields_00.hpp"
#include "phase_graph_utf8_invalid_sequences_00.hpp"
#include "phase_graph_utf8_order_00.hpp"
#include "phase_graph_utf8_valid_00.hpp"
#include "phase_graph_validation_suite_00.hpp"
#include "test_runner_support.hpp"

#include <array>
#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>

namespace {

enum class TestMode : std::uint8_t { serial, serial_reverse, mpi_2, mpi_3 };

constexpr auto serial_registrations = std::array{
    &rift_test::phase_graph_assignment_traits_00::register_tests,
    &rift_test::phase_graph_callback_bad_alloc_00::register_tests,
    &rift_test::phase_graph_canonical_tiebreak_00::register_tests,
    &rift_test::phase_graph_collective_operations_00::register_tests,
    &rift_test::phase_graph_compatibility_exception_00::register_tests,
    &rift_test::phase_graph_compatibility_order_00::register_tests,
    &rift_test::phase_graph_construct_00::register_tests,
    &rift_test::phase_graph_construct_01::register_tests,
    &rift_test::phase_graph_cycle_orientation_00::register_tests,
    &rift_test::phase_graph_errors_00::register_tests,
    &rift_test::phase_graph_input_allocation_failure_00::register_tests,
    &rift_test::phase_graph_invalid_numeric_lookup_00::register_tests,
    &rift_test::phase_graph_json_controls_00::register_tests,
    &rift_test::phase_graph_lookup_00::register_tests,
    &rift_test::phase_graph_lookup_01::register_tests,
    &rift_test::phase_graph_lookup_missing_interface_00::register_tests,
    &rift_test::phase_graph_orientation_00::register_tests,
    &rift_test::phase_graph_permutations_00::register_tests,
    &rift_test::phase_graph_post_agreement_allocation_failure_00::register_tests,
    &rift_test::phase_graph_reference_invalid_id_00::register_tests,
    &rift_test::phase_graph_reference_provenance_00::register_tests,
    &rift_test::phase_graph_reject_id_exhaustion_00::register_tests,
    &rift_test::phase_graph_serialize_00::register_tests,
    &rift_test::phase_graph_serialize_escapes_00::register_tests,
    &rift_test::phase_graph_serialize_multiple_interfaces_00::register_tests,
    &rift_test::phase_graph_utf8_fields_00::register_tests,
    &rift_test::phase_graph_utf8_invalid_sequences_00::register_tests,
    &rift_test::phase_graph_utf8_order_00::register_tests,
    &rift_test::phase_graph_utf8_valid_00::register_tests,
    &rift_test::phase_graph_validation_suite_00::register_tests,
};

template<TestMode mode> int run_subject_suite()
{
    [[maybe_unused]] boost::ut::suite<"Phase graph"> const subject_suite = [] {
        if constexpr (mode == TestMode::serial || mode == TestMode::serial_reverse) {
            rift_test::register_all<mode == TestMode::serial_reverse>(serial_registrations);
        }
        else if constexpr (mode == TestMode::mpi_2) {
            rift_test::mpi::phase_graph_callback_exception_00::register_tests();
            rift_test::mpi::phase_graph_callback_mismatch_00::register_tests();
            rift_test::mpi::phase_graph_input_mismatch_00::register_tests();
            rift_test::mpi::phase_graph_input_permutation_agreement_00::register_tests();
            rift_test::mpi::phase_graph_instance_id_agreement_00::register_tests();
        }
        else if constexpr (mode == TestMode::mpi_3) {
            rift_test::mpi::phase_graph_three_rank_agreement_00::register_tests();
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
    return rift_test::invalid_test_mode("phase_graph_run_tests", mode);
}
