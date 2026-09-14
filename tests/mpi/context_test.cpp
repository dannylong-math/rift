#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <iostream>
#include <rift/context.hpp>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
const rift::Context* test_context = nullptr;
}

static_assert(!std::is_default_constructible_v<rift::Context>);
static_assert(std::is_copy_constructible_v<rift::Context>);
static_assert(std::is_copy_assignable_v<rift::Context>);
static_assert(!std::is_constructible_v<rift::Context, int&, char**&>);

TEST_CASE("Context shares MPI services and local phase indices", "[context][mpi]")
{
    REQUIRE(test_context != nullptr);
    auto copy = *test_context;
    auto& impl = rift::detail::context_impl(copy);
    CHECK(&impl == &rift::detail::context_impl(*test_context));

    auto moved = std::move(copy);
    CHECK(&rift::detail::context_impl(moved) == &impl);
    copy = moved;
    CHECK(&rift::detail::context_impl(copy) == &impl);

    int rank = -1;
    int size = 0;
    REQUIRE(MPI_Comm_rank(MPI_COMM_WORLD, &rank) == MPI_SUCCESS);
    REQUIRE(MPI_Comm_size(MPI_COMM_WORLD, &size) == MPI_SUCCESS);
    CHECK(impl.mpi_comm() == MPI_COMM_WORLD);
    CHECK(impl.this_mpi_process() == static_cast<unsigned int>(rank));
    CHECK(impl.n_mpi_processes() == static_cast<unsigned int>(size));

    // Each rank registers a different number: registration must not synchronize.
    std::vector<int> phases;
    for (int phase = 0; phase < rank + 2; ++phase) {
        CHECK(impl.register_phase() == phases.size());
        phases.push_back(phase);
    }
    CHECK(rift::detail::context_impl(*test_context).register_phase() == phases.size());

    CHECK(impl.pcout().is_active() == (rank == 0));
    CHECK(&impl.pcout().get_stream() == &std::cout);
    CHECK(&impl.log_stream() == &rift::detail::context_impl(moved).log_stream());
    CHECK_FALSE(impl.log_stream().has_file());
    CHECK(&impl.timer() == &rift::detail::context_impl(moved).timer());

    {
        dealii::TimerOutput::Scope section(impl.timer(), "context test");
    }
    const auto calls = impl.timer().get_summary_data(dealii::TimerOutput::n_calls);
    REQUIRE(calls.size() == 1);
    CHECK(calls.at("context test") == 1.0);
}

int main(int argc, char** argv)
{
    int result = EXIT_FAILURE;
    {
        const auto context = rift::make_context(argc, argv);
        test_context = &context;
        result = Catch::Session().run(argc, argv);
        test_context = nullptr;
    }
    int finalized = 0;
    if (MPI_Finalized(&finalized) != MPI_SUCCESS || finalized == 0) {
        return EXIT_FAILURE;
    }
    return result;
}
