#include "../test_runner_support.hpp"
#include "mpi_test_registration_00.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>

int main(int argc, char** argv)
{
    const auto mode = rift_test::test_mode(argc, argv);
    if (mode != "mpi-2") {
        return rift_test::invalid_test_mode("mpi_test_infrastructure_run_tests", mode);
    }

    dealii::Utilities::MPI::MPI_InitFinalize const mpi(argc, argv, 1);
    [[maybe_unused]] boost::ut::suite<"MPI test infrastructure"> const subject_suite = [] {
        rift_test::mpi::mpi_test_registration_00::register_tests();
    };
    return static_cast<int>(boost::ut::cfg<>.run());
}
