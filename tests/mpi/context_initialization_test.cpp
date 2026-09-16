#include <array>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstdlib>
// Use the public MPI header; MPICH declares functions in an unexported nested header.
#include <mpi.h>
#include <rift/context.hpp>

TEST_CASE("Context rejects externally initialized MPI without finalizing it", "[context][mpi]")
{
    int argc = 1;
    auto program_name = std::to_array("context_initialization_test");
    std::array<char*, 2> arguments = {program_name.data(), nullptr};
    char** argv = arguments.data();

    int initialized = 0;
    // NOLINTNEXTLINE(misc-include-cleaner): the MPI API is provided by <mpi.h>.
    REQUIRE(MPI_Initialized(&initialized) == MPI_SUCCESS);
    REQUIRE(initialized != 0);
    CHECK_THROWS_WITH(rift::Context(argc, argv), Catch::Matchers::ContainsSubstring("You can only start MPI once!"));

    int finalized = 1;
    // NOLINTNEXTLINE(misc-include-cleaner): the MPI API is provided by <mpi.h>.
    REQUIRE(MPI_Finalized(&finalized) == MPI_SUCCESS);
    REQUIRE(finalized == 0);

    // The caller still owns a usable MPI session after construction fails.
    int size = 0;
    // NOLINTNEXTLINE(misc-include-cleaner): the MPI API is provided by <mpi.h>.
    REQUIRE(MPI_Comm_size(MPI_COMM_WORLD, &size) == MPI_SUCCESS);
    const int contribution = 1;
    int sum = 0;
    // NOLINTNEXTLINE(misc-include-cleaner): the MPI API is provided by <mpi.h>.
    REQUIRE(MPI_Allreduce(&contribution, &sum, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD) == MPI_SUCCESS);
    CHECK(sum == size);
}

int main(int argc, char** argv)
{
    // Deliberately initialize MPI without deal.II or Context. Keep this failure
    // scenario separate from the normal tests that let Context own MPI.
    // NOLINTNEXTLINE(misc-include-cleaner): the MPI API is provided by <mpi.h>.
    if (MPI_Init(&argc, &argv) != MPI_SUCCESS) {
        return EXIT_FAILURE;
    }
    const int result = Catch::Session().run(argc, argv);
    // NOLINTNEXTLINE(misc-include-cleaner): the MPI API is provided by <mpi.h>.
    if (MPI_Finalize() != MPI_SUCCESS) {
        return EXIT_FAILURE;
    }
    return result;
}
