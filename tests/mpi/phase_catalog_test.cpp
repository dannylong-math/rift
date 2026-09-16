#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstdint>
#include <cstdlib>
// Use the public MPI header; MPICH declares functions in an unexported nested header.
#include <mpi.h>
#include <rift/context.hpp>
#include <rift/phase.hpp>
#include <rift/phase_catalog.hpp>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

// Borrow the Context owned by main across Catch2 test callbacks.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
rift::Context* test_context = nullptr;

using Catalog = rift::PhaseCatalog<2, double>;

enum class ConstructionAction : std::uint8_t { succeed, throw_exception, inspect_candidate };

struct ConstructionControl {
    ConstructionAction action{ConstructionAction::succeed};
    int constructions{0};
    int live_instances{0};
    const Catalog* catalog{nullptr};
};

class ConfigurablePhase final : public rift::Phase<2, double> {
public:
    static constexpr std::string_view model_identifier() noexcept { return "configurable-model"; }
    static constexpr std::string_view discretization_identifier() noexcept { return "configurable-discretization"; }

    ConfigurablePhase(rift::PhaseDescriptor descriptor, ConstructionControl& control) :
        Phase(std::move(descriptor)), live_instances_(&control.live_instances)
    {
        switch (control.action) {
        case ConstructionAction::succeed:
            ++control.constructions;
            ++control.live_instances;
            owns_live_instance_ = true;
            break;
        case ConstructionAction::throw_exception:
            throw std::runtime_error("intentional phase construction failure");
        case ConstructionAction::inspect_candidate:
            if (control.catalog == nullptr) {
                throw std::logic_error("candidate inspection requires a catalog");
            }
            static_cast<void>(control.catalog->at(this->descriptor().id()));
            break;
        }
    }

    ConfigurablePhase(const ConfigurablePhase&) = delete;
    ConfigurablePhase& operator=(const ConfigurablePhase&) = delete;
    ConfigurablePhase(ConfigurablePhase&&) = delete;
    ConfigurablePhase& operator=(ConfigurablePhase&&) = delete;
    ~ConfigurablePhase() override
    {
        if (owns_live_instance_) {
            --*live_instances_;
        }
    }

private:
    int* live_instances_;
    bool owns_live_instance_{false};
};

class AlternatePhase final : public rift::Phase<2, double> {
public:
    static constexpr std::string_view model_identifier() noexcept { return "alternate-model"; }
    static constexpr std::string_view discretization_identifier() noexcept { return "alternate-discretization"; }

    explicit AlternatePhase(rift::PhaseDescriptor descriptor) : Phase(std::move(descriptor)) {}
};

static_assert(std::is_constructible_v<Catalog, rift::Context&>);
static_assert(!std::is_copy_constructible_v<Catalog>);
static_assert(!std::is_copy_assignable_v<Catalog>);
static_assert(!std::is_move_constructible_v<Catalog>);
static_assert(!std::is_move_assignable_v<Catalog>);

} // namespace

// Catch2 assertion expansions inflate complexity; this scenario deliberately
// checks the complete rank-local catalog lifecycle against one Context claim.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("PhaseCatalog owns phases and freezes rank-local registration", "[phase_catalog][mpi]")
{
    REQUIRE(test_context != nullptr);

    ConstructionControl construction;
    {
        Catalog catalog(*test_context);
        CHECK(catalog.empty());
        // Exercise size() independently from the equivalent empty() query.
        // NOLINTNEXTLINE(readability-container-size-empty)
        CHECK(catalog.size() == 0);
        CHECK_FALSE(catalog.is_frozen());

        // NOLINTNEXTLINE(misc-include-cleaner): provided by catch_test_macros.hpp.
        CHECK_THROWS_WITH(catalog.emplace<AlternatePhase>(""), Catch::Matchers::ContainsSubstring("non-whitespace"));
        CHECK_THROWS_WITH(catalog.emplace<AlternatePhase>(" \t\n"),
                          Catch::Matchers::ContainsSubstring("non-whitespace"));
        CHECK(catalog.empty());

        const auto water_id = catalog.emplace<ConfigurablePhase>("water", construction);
        const auto spaced_water_id = catalog.emplace<AlternatePhase>(" water ");
        CHECK(water_id.index() == 0);
        CHECK(spaced_water_id.index() == 1);
        CHECK(construction.constructions == 1);
        CHECK(construction.live_instances == 1);
        CHECK(catalog.size() == 2);

        const auto& water = catalog.at(water_id).descriptor();
        CHECK(water.id() == water_id);
        CHECK(water.name() == "water");
        CHECK(water.model_id().value() == "configurable-model");
        CHECK(water.discretization_id().value() == "configurable-discretization");

        const auto& spaced_water = catalog.at(spaced_water_id).descriptor();
        CHECK(spaced_water.name() == " water ");
        CHECK(spaced_water.model_id().value() == "alternate-model");
        CHECK(spaced_water.discretization_id().value() == "alternate-discretization");

        CHECK_THROWS_WITH(catalog.emplace<AlternatePhase>("water"),
                          Catch::Matchers::ContainsSubstring("already registered"));
        CHECK(catalog.size() == 2);

        construction.action = ConstructionAction::throw_exception;
        CHECK_THROWS_WITH(catalog.emplace<ConfigurablePhase>("vapor", construction),
                          Catch::Matchers::ContainsSubstring("intentional phase construction failure"));
        CHECK(catalog.size() == 2);

        construction.action = ConstructionAction::inspect_candidate;
        construction.catalog = &catalog;
        CHECK_THROWS_WITH(catalog.emplace<ConfigurablePhase>("candidate", construction),
                          Catch::Matchers::ContainsSubstring("outside this PhaseCatalog"));
        CHECK(catalog.size() == 2);

        construction.action = ConstructionAction::succeed;
        catalog.freeze_local();
        catalog.freeze_local();
        CHECK(catalog.is_frozen());
        CHECK_THROWS_WITH(catalog.emplace<ConfigurablePhase>("ice", construction),
                          Catch::Matchers::ContainsSubstring("frozen"));
        CHECK_THROWS_WITH(catalog.emplace<AlternatePhase>("snow"), Catch::Matchers::ContainsSubstring("frozen"));
        CHECK(construction.constructions == 1);
        CHECK(construction.live_instances == 1);
        CHECK(catalog.size() == 2);
    }
    CHECK(construction.live_instances == 0);

    CHECK_THROWS_WITH(Catalog(*test_context), Catch::Matchers::ContainsSubstring("already been claimed"));
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
