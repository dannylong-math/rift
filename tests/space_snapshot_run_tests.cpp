#include "space_snapshot_missing_field_00.hpp"
#include "space_snapshot_reject_wrong_phase_00.hpp"
#include "test_runner_support.hpp"

#include <array>
#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>

namespace {

constexpr auto serial_registrations = std::array{
    &rift_test::space_snapshot_missing_field_00::register_tests,
    &rift_test::space_snapshot_reject_wrong_phase_00::register_tests,
};

template<bool reverse> int run_initialized(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize const mpi(argc, argv, 1);

    [[maybe_unused]] boost::ut::suite<"Space snapshot"> const subject_suite = [] {
        rift_test::register_all<reverse>(serial_registrations);
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}

} // namespace

int main(int argc, char** argv)
{
    const auto mode = rift_test::test_mode(argc, argv);
    if (mode == "serial") {
        return run_initialized<false>(argc, argv);
    }
    if (mode == "serial-reverse") {
        return run_initialized<true>(argc, argv);
    }
    return rift_test::invalid_test_mode("space_snapshot_run_tests", mode);
}
