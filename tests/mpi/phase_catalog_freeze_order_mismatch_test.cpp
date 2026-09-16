#include <array>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <deal.II/base/exceptions.h>
// Use the public MPI header; MPICH declares functions in an unexported nested header.
#include <mpi.h>
#include <rift/context.hpp>
#include <rift/phase.hpp>
#include <rift/phase_catalog.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

[[nodiscard]] std::string freeze_diagnostic(Catalog& catalog)
{
    try {
        catalog.freeze();
    }
    catch (const dealii::ExceptionBase& exception) {
        std::ostringstream diagnostic;
        exception.print_info(diagnostic);
        return std::move(diagnostic).str();
    }
    return {};
}

} // namespace

// Catch2 assertion expansions dominate this intentionally complete MPI scenario.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("PhaseCatalog reports the first immutable ordering mismatch", "[phase_catalog][mpi]")
{
    REQUIRE(test_context != nullptr);

    const std::array<std::string_view, 3> canonical_names{"water", "oil", "vapor"};
    auto local_names = canonical_names;
    if (test_context->this_mpi_process() == 1) {
        local_names = {"water", "vapor", "oil"};
    }
    else if (test_context->this_mpi_process() == 2) {
        local_names = {"ice", "oil", "vapor"};
    }

    Catalog catalog(*test_context);
    std::vector<rift::PhaseId> ids;
    ids.reserve(local_names.size());
    for (const auto name : local_names) {
        ids.push_back(catalog.emplace<TestPhase>(std::string(name)));
    }

    if (test_context->n_mpi_processes() == 1) {
        CHECK(freeze_diagnostic(catalog).empty());
        CHECK(catalog.is_frozen());
        return;
    }

    constexpr std::string_view expected_diagnostic =
        "    PhaseCatalog mismatch at rank 1, phase 1, field 'name' (rank 0: 'oil', rank 1: 'vapor').\n";
    CHECK(freeze_diagnostic(catalog) == expected_diagnostic);
    CHECK_FALSE(catalog.is_frozen());
    CHECK(catalog.size() == local_names.size());
    for (std::size_t phase = 0; phase < local_names.size(); ++phase) {
        CHECK(catalog.at(ids.at(phase)).descriptor().name() == local_names.at(phase));
    }

    const auto suffix_id = catalog.emplace<TestPhase>("shared-suffix");
    CHECK(suffix_id.index() == canonical_names.size());
    CHECK(freeze_diagnostic(catalog) == expected_diagnostic);
    CHECK_FALSE(catalog.is_frozen());
    CHECK(catalog.size() == canonical_names.size() + 1);
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
