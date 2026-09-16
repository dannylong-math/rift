#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <deal.II/base/conditional_ostream.h>
#include <deal.II/base/enable_observer_pointer.h>
#include <deal.II/base/logstream.h>
#include <deal.II/base/observer_pointer.h>
#include <deal.II/base/timer.h>
#include <iostream>
// Use the public MPI header; MPICH declares functions in an unexported nested header.
#include <mpi.h>
#include <rift/context.hpp>
#include <type_traits>

namespace {
// Borrow the mutable Context owned by main across Catch2 test callbacks.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
rift::Context* test_context = nullptr;
} // namespace

static_assert(!std::is_default_constructible_v<rift::Context>);
static_assert(std::is_constructible_v<rift::Context, int&, char**&>);
static_assert(!std::is_copy_constructible_v<rift::Context>);
static_assert(!std::is_copy_assignable_v<rift::Context>);
static_assert(!std::is_move_constructible_v<rift::Context>);
static_assert(!std::is_move_assignable_v<rift::Context>);
static_assert(std::is_base_of_v<dealii::EnableObserverPointer, rift::Context>);
static_assert(std::is_same_v<decltype(&rift::Context::id), const rift::Context* (rift::Context::*)() const noexcept>);
static_assert(
    std::is_same_v<decltype(&rift::Context::pcout), dealii::ConditionalOStream& (rift::Context::*)() noexcept>);
static_assert(std::is_same_v<decltype(&rift::Context::log_stream), dealii::LogStream& (rift::Context::*)() noexcept>);
static_assert(std::is_same_v<decltype(&rift::Context::timer), dealii::TimerOutput& (rift::Context::*)() noexcept>);

// Catch2 assertion expansions inflate complexity; preserve the shared lifetime scenario.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("Context observers share stable identity and mutable services", "[context][mpi]")
{
    REQUIRE(test_context != nullptr);
    const auto* const identity = test_context->id();
    CHECK(identity == test_context);
    const auto subscriptions = test_context->n_subscriptions();
    {
        const dealii::ObserverPointer<rift::Context> first(test_context, "first test observer");
        const dealii::ObserverPointer<rift::Context> second(test_context, "second test observer");
        CHECK(test_context->n_subscriptions() == subscriptions + 2);
        CHECK(first.get() == test_context);
        CHECK(second.get() == test_context);
        CHECK(first->id() == identity);
        CHECK(second->id() == identity);

        int rank = -1;
        int size = 0;
        // NOLINTNEXTLINE(misc-include-cleaner): the MPI API is provided by <mpi.h>.
        REQUIRE(MPI_Comm_rank(MPI_COMM_WORLD, &rank) == MPI_SUCCESS);
        // NOLINTNEXTLINE(misc-include-cleaner): the MPI API is provided by <mpi.h>.
        REQUIRE(MPI_Comm_size(MPI_COMM_WORLD, &size) == MPI_SUCCESS);
        CHECK(test_context->mpi_comm() == MPI_COMM_WORLD);
        CHECK(test_context->this_mpi_process() == static_cast<unsigned int>(rank));
        CHECK(test_context->n_mpi_processes() == static_cast<unsigned int>(size));

        CHECK(test_context->id() == identity);

        CHECK(first->pcout().is_active() == (rank == 0));
        CHECK(&first->pcout().get_stream() == &std::cout);
        CHECK(&first->pcout() == &second->pcout());
        CHECK(&test_context->pcout() == &first->pcout());
        CHECK(&first->log_stream() == &second->log_stream());
        CHECK(&test_context->log_stream() == &first->log_stream());
        CHECK_FALSE(first->log_stream().has_file());
        CHECK(&first->timer() == &second->timer());
        CHECK(&test_context->timer() == &first->timer());

        {
            const dealii::TimerOutput::Scope section(test_context->timer(), "context test");
        }
        const auto calls = second->timer().get_summary_data(dealii::TimerOutput::n_calls);
        REQUIRE(calls.size() == 1);
        CHECK(calls.at("context test") == 1.0);
    }
    CHECK(test_context->n_subscriptions() == subscriptions);
    CHECK(test_context->id() == identity);
}

int main(int argc, char** argv)
{
    int result = EXIT_FAILURE;
    {
        rift::Context context(argc, argv);
        test_context = &context;
        result = Catch::Session().run(argc, argv);
        test_context = nullptr;
    }
    int finalized = 0;
    // NOLINTNEXTLINE(misc-include-cleaner): the MPI API is provided by <mpi.h>.
    if (MPI_Finalized(&finalized) != MPI_SUCCESS || finalized == 0) {
        return EXIT_FAILURE;
    }
    return result;
}
