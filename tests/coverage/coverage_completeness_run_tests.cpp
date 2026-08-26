#include "../test_runner_support.hpp"
#include "coverage_completeness_guard_00.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>

int main(int argc, char** argv)
{
    const auto mode = rift_test::test_mode(argc, argv);
    if (mode != "coverage") {
        return rift_test::invalid_test_mode("coverage_completeness_run_tests", mode);
    }

    const dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    [[maybe_unused]] boost::ut::suite<"Coverage completeness"> const subject_suite = [] {
        rift_test::coverage_completeness_guard_00::register_tests();
    };
    return static_cast<int>(boost::ut::cfg<>.run());
}
