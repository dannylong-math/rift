#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <deal.II/base/exceptions.h>
// Use the public MPI header; MPICH declares functions in an unexported nested header.
#include <mpi.h>
#include <optional>
#include <rift/context.hpp>
#include <rift/phase.hpp>
#include <rift/phase_catalog.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace {

// Borrow the Context owned by main across Catch2 test callbacks.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
rift::Context* test_context = nullptr;

using Catalog = rift::PhaseCatalog<2, double>;

class TestPhase final : public rift::Phase<2, double> {
public:
    static constexpr std::string_view model_identifier() noexcept { return "test-model"; }
    static constexpr std::string_view discretization_identifier() noexcept { return "test-discretization"; }

    explicit TestPhase(rift::PhaseDescriptor descriptor) : Phase(std::move(descriptor)) {}
};

} // namespace

// Catch2 assertion expansions dominate this intentionally complete MPI scenario.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("PhaseCatalog remains open after a repairable collective mismatch", "[phase_catalog][mpi]")
{
    REQUIRE(test_context != nullptr);

    Catalog catalog(*test_context);
    const auto water_id = catalog.emplace<TestPhase>("water");
    const bool omit_suffix = test_context->n_mpi_processes() > 1 && test_context->this_mpi_process() == 1;
    std::optional<rift::PhaseId> vapor_id;

    if (!omit_suffix) {
        vapor_id = catalog.emplace<TestPhase>("vapor");
        CHECK(vapor_id->index() == 1);
    }

    if (test_context->n_mpi_processes() > 1) {
        std::ostringstream diagnostic;
        bool threw = false;
        try {
            catalog.freeze();
        }
        catch (const dealii::ExceptionBase& exception) {
            threw = true;
            exception.print_info(diagnostic);
        }
        CHECK(threw);
        CHECK(diagnostic.str() == "    PhaseCatalog mismatch at rank 1: phase count differs (rank 0: 2, rank 1: 1).\n");
        CHECK_FALSE(catalog.is_frozen());
        CHECK(catalog.size() == (omit_suffix ? 1 : 2));

        if (omit_suffix) {
            vapor_id = catalog.emplace<TestPhase>("vapor");
            CHECK(vapor_id->index() == 1);
        }
    }

    REQUIRE(vapor_id.has_value());
    catalog.freeze();
    CHECK(catalog.is_frozen());
    CHECK(catalog.size() == 2);
    CHECK(catalog.at(water_id).descriptor().name() == "water");
    // REQUIRE above establishes that every rank retained its returned vapor ID.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    CHECK(catalog.at(*vapor_id).descriptor().name() == "vapor");
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
