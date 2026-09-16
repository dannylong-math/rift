#include <catch2/catch_test_macros.hpp>
#include <rift/phase.hpp>
#include <string>
#include <type_traits>

static_assert(!std::is_default_constructible_v<rift::PhaseId>);
static_assert(std::is_copy_constructible_v<rift::PhaseId>);
static_assert(std::is_move_constructible_v<rift::PhaseId>);
static_assert(!std::is_convertible_v<std::size_t, rift::PhaseId>);

static_assert(!std::is_default_constructible_v<rift::PhaseDescriptor>);
static_assert(std::is_copy_constructible_v<rift::PhaseDescriptor>);
static_assert(std::is_move_constructible_v<rift::PhaseDescriptor>);
static_assert(!std::is_copy_assignable_v<rift::PhaseDescriptor>);
static_assert(!std::is_move_assignable_v<rift::PhaseDescriptor>);

static_assert(std::is_abstract_v<rift::Phase<2, double>>);
static_assert(std::has_virtual_destructor_v<rift::Phase<2, double>>);
static_assert(!std::is_copy_constructible_v<rift::Phase<2, double>>);
static_assert(!std::is_move_constructible_v<rift::Phase<2, double>>);
static_assert(!std::is_same_v<rift::ModelId, rift::DiscretizationId>);

TEST_CASE("Phase metadata identifiers are distinct owning values", "[phase]")
{
    const std::string model_source = "incompressible-navier-stokes";
    const std::string discretization_source = "matrix-free-test-policy";

    const rift::ModelId model(model_source);
    const rift::ModelId same_model(model_source);
    const rift::ModelId other_model("low-mach-reactive-flow");
    const rift::DiscretizationId discretization(discretization_source);
    const rift::DiscretizationId same_discretization(discretization_source);
    const rift::DiscretizationId other_discretization("another-policy");

    CHECK(model.value() == model_source);
    CHECK(model == same_model);
    CHECK(model != other_model);
    CHECK(discretization.value() == discretization_source);
    CHECK(discretization == same_discretization);
    CHECK(discretization != other_discretization);
}
