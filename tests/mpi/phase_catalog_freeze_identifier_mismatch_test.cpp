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

namespace {

// Borrow the Context owned by main across Catch2 test callbacks.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
rift::Context* test_context = nullptr;

using Catalog = rift::PhaseCatalog<2, double>;

class ReferencePhase final : public rift::Phase<2, double> {
public:
    static constexpr std::string_view model_identifier() noexcept { return "reference-model"; }
    static constexpr std::string_view discretization_identifier() noexcept { return "reference-discretization"; }

    explicit ReferencePhase(rift::PhaseDescriptor descriptor) : Phase(std::move(descriptor)) {}
};

class ModelMismatchPhase final : public rift::Phase<2, double> {
public:
    static constexpr std::string_view model_identifier() noexcept { return "alternate-model"; }
    static constexpr std::string_view discretization_identifier() noexcept { return "reference-discretization"; }

    explicit ModelMismatchPhase(rift::PhaseDescriptor descriptor) : Phase(std::move(descriptor)) {}
};

class DiscretizationMismatchPhase final : public rift::Phase<2, double> {
public:
    static constexpr std::string_view model_identifier() noexcept { return "reference-model"; }
    static constexpr std::string_view discretization_identifier() noexcept { return "alternate-discretization"; }

    explicit DiscretizationMismatchPhase(rift::PhaseDescriptor descriptor) : Phase(std::move(descriptor)) {}
};

struct LocalPhase {
    rift::PhaseId id;
    std::string_view model_id;
    std::string_view discretization_id;
};

[[nodiscard]] LocalPhase emplace_local_phase(Catalog& catalog, const unsigned int size, const unsigned int rank)
{
    if (size == 2 && rank == 1) {
        return {.id = catalog.emplace<ModelMismatchPhase>("fluid"),
                .model_id = ModelMismatchPhase::model_identifier(),
                .discretization_id = ModelMismatchPhase::discretization_identifier()};
    }
    if (size == 3 && rank == 1) {
        return {.id = catalog.emplace<DiscretizationMismatchPhase>("fluid"),
                .model_id = DiscretizationMismatchPhase::model_identifier(),
                .discretization_id = DiscretizationMismatchPhase::discretization_identifier()};
    }
    if (size == 3 && rank == 2) {
        return {.id = catalog.emplace<ModelMismatchPhase>("fluid"),
                .model_id = ModelMismatchPhase::model_identifier(),
                .discretization_id = ModelMismatchPhase::discretization_identifier()};
    }
    return {.id = catalog.emplace<ReferencePhase>("fluid"),
            .model_id = ReferencePhase::model_identifier(),
            .discretization_id = ReferencePhase::discretization_identifier()};
}

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
TEST_CASE("PhaseCatalog reports model and discretization identifier mismatches", "[phase_catalog][mpi]")
{
    REQUIRE(test_context != nullptr);

    Catalog catalog(*test_context);
    const unsigned int size = test_context->n_mpi_processes();
    const LocalPhase local_phase = emplace_local_phase(catalog, size, test_context->this_mpi_process());

    if (size == 1) {
        CHECK(freeze_diagnostic(catalog).empty());
        CHECK(catalog.is_frozen());
        return;
    }

    constexpr std::string_view model_diagnostic =
        "    PhaseCatalog mismatch at rank 1, phase 0, field 'model id' (rank 0: 'reference-model', rank 1: "
        "'alternate-model').\n";
    constexpr std::string_view discretization_diagnostic =
        "    PhaseCatalog mismatch at rank 1, phase 0, field 'discretization id' (rank 0: "
        "'reference-discretization', rank 1: 'alternate-discretization').\n";
    CHECK(freeze_diagnostic(catalog) == (size == 2 ? model_diagnostic : discretization_diagnostic));
    CHECK_FALSE(catalog.is_frozen());
    CHECK(catalog.size() == 1);

    const auto& descriptor = catalog.at(local_phase.id).descriptor();
    CHECK(descriptor.name() == "fluid");
    CHECK(descriptor.model_id().value() == local_phase.model_id);
    CHECK(descriptor.discretization_id().value() == local_phase.discretization_id);
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
