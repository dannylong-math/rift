#include <algorithm>
#include <array>
#include <boost/ut.hpp>
#include <cstddef>
#include <cstdint>
#include <rift/phase_graph.hpp>
#include <rift/rift_context.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// Boost.UT suites cannot capture runtime state, so main initializes these once.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
const rift::PhaseGraph* graph_ptr = nullptr;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::string creation_failure;

static_assert(
    std::is_same_v<decltype(std::declval<const rift::PhaseGraph&>().phases()), std::span<const rift::PhaseDescriptor>>);
static_assert(std::is_same_v<decltype(std::declval<const rift::PhaseGraph&>().interfaces()),
                             std::span<const rift::InterfaceDescriptor>>);
static_assert(std::is_const_v<std::remove_reference_t<
                  decltype(std::declval<const rift::PhaseGraph&>().phase(rift::PhaseId::from_index(0)))>>);
static_assert(
    std::is_const_v<std::remove_reference_t<
        decltype(std::declval<const rift::PhaseGraph&>().material_interface(rift::InterfaceId::from_index(0)))>>);

[[nodiscard]] rift::PhaseSpecification phase(std::string name, std::string physics)
{
    return {.name = std::move(name), .physics_key = rift::PhysicsKey{std::move(physics)}};
}

[[nodiscard]] rift::InterfaceSpecification interface(std::string name, std::string minus, std::string plus,
                                                     std::string operation)
{
    return {.name = std::move(name),
            .minus_phase = std::move(minus),
            .plus_phase = std::move(plus),
            .operator_key = rift::InterfaceOperatorKey{std::move(operation)}};
}

template<class Value> void permute_for_rank(std::vector<Value>& values, const unsigned int rank)
{
    const auto offset = static_cast<std::ptrdiff_t>(rank % values.size());
    std::ranges::rotate(values, values.begin() + offset);
}

[[nodiscard]] rift::PhaseGraphSpecification publication_specification(const unsigned int rank)
{
    rift::PhaseGraphSpecification specification{
        .phases = {phase("water", "incompressible"), phase("air", "ideal-gas"), phase("oil", "viscous"),
                   phase("solid", "elastic")},
        .interfaces = {interface("water-solid", "water", "solid", "wetting"),
                       interface("free-surface", "water", "air", "surface-tension"),
                       interface("air-solid", "solid", "air", "contact")},
    };
    permute_for_rank(specification.phases, rank);
    permute_for_rank(specification.interfaces, rank);
    return specification;
}

void expect_graph_available() { boost::ut::expect(graph_ptr != nullptr) << creation_failure; }

void test_descriptor_views()
{
    using namespace boost::ut;
    expect_graph_available();
    if (graph_ptr == nullptr) {
        return;
    }

    struct ExpectedPhase {
        std::uint32_t id;
        std::string_view name;
        std::string_view physics;
    };
    constexpr std::array<ExpectedPhase, 4> expected_phases{{
        ExpectedPhase{.id = 0, .name = "air", .physics = "ideal-gas"},
        ExpectedPhase{.id = 1, .name = "oil", .physics = "viscous"},
        ExpectedPhase{.id = 2, .name = "solid", .physics = "elastic"},
        ExpectedPhase{.id = 3, .name = "water", .physics = "incompressible"},
    }};

    const auto phases = graph_ptr->phases();
    expect(phases.size() == expected_phases.size());
    auto phase_position = phases.begin();
    for (const auto& expected : expected_phases) {
        if (phase_position == phases.end()) {
            break;
        }
        const auto& actual = *phase_position;
        ++phase_position;
        expect(actual.id.value() == expected.id);
        expect(actual.name == expected.name);
        expect(actual.physics_key.value() == expected.physics);
        expect(&graph_ptr->phase(actual.id) == &actual);
    }

    struct ExpectedInterface {
        std::uint32_t id;
        std::string_view name;
        std::uint32_t minus_phase;
        std::uint32_t plus_phase;
        std::string_view operation;
    };
    constexpr std::array<ExpectedInterface, 3> expected_interfaces{{
        ExpectedInterface{.id = 0, .name = "air-solid", .minus_phase = 2, .plus_phase = 0, .operation = "contact"},
        ExpectedInterface{
            .id = 1, .name = "free-surface", .minus_phase = 3, .plus_phase = 0, .operation = "surface-tension"},
        ExpectedInterface{.id = 2, .name = "water-solid", .minus_phase = 3, .plus_phase = 2, .operation = "wetting"},
    }};

    const auto interfaces = graph_ptr->interfaces();
    expect(interfaces.size() == expected_interfaces.size());
    auto interface_position = interfaces.begin();
    for (const auto& expected : expected_interfaces) {
        if (interface_position == interfaces.end()) {
            break;
        }
        const auto& actual = *interface_position;
        ++interface_position;
        expect(actual.id.value() == expected.id);
        expect(actual.name == expected.name);
        expect(actual.minus_phase.value() == expected.minus_phase);
        expect(actual.plus_phase.value() == expected.plus_phase);
        expect(actual.operator_key.value() == expected.operation);
        expect(&graph_ptr->material_interface(actual.id) == &actual);
    }
}

void test_checked_lookup()
{
    using namespace boost::ut;
    expect_graph_available();
    if (graph_ptr == nullptr) {
        return;
    }

    const auto air = graph_ptr->find_phase("air");
    const auto water = graph_ptr->find_phase("water");
    expect(air.has_value());
    expect(water.has_value());
    if (air.has_value()) {
        expect(air->value() == std::uint32_t{0});
    }
    if (water.has_value()) {
        expect(water->value() == std::uint32_t{3});
    }
    expect(not graph_ptr->find_phase("Air").has_value());
    expect(not graph_ptr->find_phase("vapor").has_value());
    expect(not graph_ptr->find_phase("z-phase").has_value());

    const auto first_interface = graph_ptr->find_interface("air-solid");
    const auto last_interface = graph_ptr->find_interface("water-solid");
    expect(first_interface.has_value());
    expect(last_interface.has_value());
    if (first_interface.has_value()) {
        expect(first_interface->value() == std::uint32_t{0});
    }
    if (last_interface.has_value()) {
        expect(last_interface->value() == std::uint32_t{2});
    }
    expect(not graph_ptr->find_interface("Free-surface").has_value());
    expect(not graph_ptr->find_interface("missing").has_value());
    expect(not graph_ptr->find_interface("z-interface").has_value());

    expect(throws<std::out_of_range>([] { static_cast<void>(graph_ptr->phase(rift::PhaseId::from_index(4))); }));
    expect(throws<std::out_of_range>(
        [] { static_cast<void>(graph_ptr->material_interface(rift::InterfaceId::from_index(3))); }));
}

void expect_interface_pair(const rift::PhaseId first, const rift::PhaseId second, const std::uint32_t expected)
{
    const auto found = graph_ptr->find_interface(first, second);
    boost::ut::expect(found.has_value());
    if (found.has_value()) {
        boost::ut::expect(found->value() == expected);
    }
}

void test_unordered_adjacency_lookup()
{
    using namespace boost::ut;
    expect_graph_available();
    if (graph_ptr == nullptr) {
        return;
    }

    const auto air = rift::PhaseId::from_index(0);
    const auto oil = rift::PhaseId::from_index(1);
    const auto solid = rift::PhaseId::from_index(2);
    const auto water = rift::PhaseId::from_index(3);
    const auto invalid = rift::PhaseId::from_index(99);

    expect_interface_pair(water, air, 1);
    expect_interface_pair(air, water, 1);
    expect(not graph_ptr->find_interface(air, oil).has_value());
    expect(not graph_ptr->find_interface(solid, solid).has_value());
    expect(not graph_ptr->find_interface(air, invalid).has_value());
    expect(not graph_ptr->find_interface(invalid, air).has_value());
}

void test_detached_descriptor_copies()
{
    using namespace boost::ut;
    expect_graph_available();
    if (graph_ptr == nullptr) {
        return;
    }

    auto phase_copy = graph_ptr->phase(rift::PhaseId::from_index(0));
    phase_copy.name = "changed";
    expect(graph_ptr->phase(rift::PhaseId::from_index(0)).name == "air");

    auto interface_copy = graph_ptr->material_interface(rift::InterfaceId::from_index(0));
    interface_copy.name = "changed";
    interface_copy.minus_phase = rift::PhaseId::from_index(0);
    expect(graph_ptr->material_interface(rift::InterfaceId::from_index(0)).name == "air-solid");
    expect(graph_ptr->material_interface(rift::InterfaceId::from_index(0)).minus_phase.value() == std::uint32_t{2});
}

} // namespace

int main(int argc, char** argv)
{
    using namespace boost::ut;

    rift::RiftContext context(argc, argv);
    const auto result = context.create_phase_graph(publication_specification(context.this_mpi_process()),
                                                   rift::interface_compatibility::accept_all);
    if (result.has_value()) {
        graph_ptr = &result->get();
    }
    else {
        creation_failure = rift::format_phase_graph_errors(result.error());
    }

    [[maybe_unused]] const suite<"Published phase graph"> suite = [] {
        "descriptor views match an independent canonical table"_test = test_descriptor_views;
        "numeric and name lookup are checked"_test = test_checked_lookup;
        "phase-pair lookup is unordered and checked"_test = test_unordered_adjacency_lookup;
        "detached descriptor copies cannot mutate the graph"_test = test_detached_descriptor_copies;
    };

    const auto test_result = static_cast<int>(cfg<>.run());
    graph_ptr = nullptr;
    creation_failure.clear();
    return test_result;
}
