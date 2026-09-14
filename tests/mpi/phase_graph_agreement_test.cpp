#include <algorithm>
#include <boost/ut.hpp>
#include <cstddef>
#include <expected>
#include <format>
#include <functional>
#include <initializer_list>
#include <rift/phase_graph.hpp>
#include <rift/rift_context.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

static_assert(not std::is_copy_constructible_v<rift::PhaseGraph>);
static_assert(not std::is_copy_assignable_v<rift::PhaseGraph>);
static_assert(not std::is_move_constructible_v<rift::PhaseGraph>);
static_assert(not std::is_move_assignable_v<rift::PhaseGraph>);
static_assert(std::is_same_v<rift::PhaseGraphResult::value_type, std::reference_wrapper<const rift::PhaseGraph>>);

[[nodiscard]] rift::PhaseSpecification phase(std::string name, std::string physics_key)
{
    return {.name = std::move(name), .physics_key = rift::PhysicsKey{std::move(physics_key)}};
}

[[nodiscard]] rift::InterfaceSpecification interface(std::string name, std::string minus_phase, std::string plus_phase,
                                                     std::string operator_key)
{
    return {.name = std::move(name),
            .minus_phase = std::move(minus_phase),
            .plus_phase = std::move(plus_phase),
            .operator_key = rift::InterfaceOperatorKey{std::move(operator_key)}};
}

template<class Value> void permute_for_rank(std::vector<Value>& values, const unsigned int rank)
{
    if (values.size() < 2) {
        return;
    }
    if (rank % 3 == 1) {
        std::rotate(values.begin(), values.begin() + 1, values.end());
    }
    else if (rank % 3 == 2) {
        std::reverse(values.begin(), values.end());
    }
}

[[nodiscard]] rift::PhaseGraphSpecification triangle_specification(const unsigned int rank)
{
    rift::PhaseGraphSpecification specification{
        .phases = {phase("water", "incompressible"), phase("air", "ideal-gas"), phase("solid", "elastic")},
        .interfaces = {interface("water-solid", "water", "solid", "wetting"),
                       interface("air-solid", "solid", "air", "contact"),
                       interface("free-surface", "water", "air", "surface-tension")},
    };
    permute_for_rank(specification.phases, rank);
    permute_for_rank(specification.interfaces, rank);
    return specification;
}

[[nodiscard]] rift::PhaseGraphSpecification single_interface_specification()
{
    return {
        .phases = {phase("liquid", "incompressible"), phase("gas", "ideal-gas")},
        .interfaces = {interface("free-surface", "liquid", "gas", "surface-tension")},
    };
}

[[nodiscard]] std::size_t error_count(const rift::PhaseGraphErrors& errors, const rift::PhaseGraphErrorCode code)
{
    return static_cast<std::size_t>(
        std::count_if(errors.begin(), errors.end(), [code](const auto& error) { return error.code == code; }));
}

void expect_single_error(const rift::PhaseGraphResult& result, const rift::PhaseGraphErrorCode code,
                         const std::initializer_list<std::string_view> message_fragments = {})
{
    using namespace boost::ut;

    expect(not result.has_value());
    if (result.has_value()) {
        return;
    }

    expect(result.error().size() == std::size_t{1});
    if (result.error().size() != 1) {
        return;
    }

    const auto& error = result.error().front();
    expect(error.code == code);
    for (const auto fragment : message_fragments) {
        expect(error.message.contains(fragment));
    }
}

void expect_message_contains(const rift::PhaseGraphError& error,
                             const std::initializer_list<std::string_view> fragments)
{
    for (const auto fragment : fragments) {
        boost::ut::expect(error.message.contains(fragment));
    }
}

[[nodiscard]] std::string compatibility_observation(const rift::PhaseSpecification& minus_phase,
                                                    const rift::PhaseSpecification& plus_phase,
                                                    const rift::InterfaceSpecification& interface)
{
    return std::format("{}:{}|{}:{}|{}:{}", interface.name, interface.operator_key.value(), minus_phase.name,
                       minus_phase.physics_key.value(), plus_phase.name, plus_phase.physics_key.value());
}

void test_successful_agreement(rift::RiftContext& context)
{
    using namespace boost::ut;

    std::vector<std::string> observations;
    auto result = context.create_phase_graph(
        triangle_specification(context.this_mpi_process()),
        [&observations](const auto& minus_phase, const auto& plus_phase,
                        const auto& interface) -> rift::InterfaceCompatibilityDecision {
            observations.push_back(compatibility_observation(minus_phase, plus_phase, interface));
            return {};
        });

    expect(result.has_value());
    const std::vector<std::string> expected_observations{
        "air-solid:contact|solid:elastic|air:ideal-gas",
        "free-surface:surface-tension|water:incompressible|air:ideal-gas",
        "water-solid:wetting|water:incompressible|solid:elastic",
    };
    expect(observations == expected_observations);

    const auto second_result = context.create_phase_graph({}, {});
    expect_single_error(second_result, rift::PhaseGraphErrorCode::phase_graph_creation_already_attempted,
                        {"single phase-graph creation attempt"});
}

void test_graph_without_interfaces(rift::RiftContext& context)
{
    rift::PhaseGraphSpecification specification{.phases = {phase("fluid", "incompressible")}, .interfaces = {}};
    const auto result = context.create_phase_graph(std::move(specification), {});
    boost::ut::expect(result.has_value());
}

void test_accept_all_policy(rift::RiftContext& context)
{
    const auto result =
        context.create_phase_graph(single_interface_specification(), rift::interface_compatibility::accept_all);
    boost::ut::expect(result.has_value());
}

void test_no_phases_and_sealed_failure(rift::RiftContext& context)
{
    auto result = context.create_phase_graph({}, {});
    expect_single_error(result, rift::PhaseGraphErrorCode::no_phases, {"at least one phase"});

    result = context.create_phase_graph(single_interface_specification(), rift::interface_compatibility::accept_all);
    expect_single_error(result, rift::PhaseGraphErrorCode::phase_graph_creation_already_attempted,
                        {"single phase-graph creation attempt"});
}

[[nodiscard]] std::string invalid_utf8()
{
    std::string value;
    value.push_back(static_cast<char>(0xC3));
    value.push_back(static_cast<char>(0x28));
    return value;
}

[[nodiscard]] rift::PhaseGraphSpecification invalid_specification(const unsigned int rank)
{
    const auto invalid = invalid_utf8();
    rift::PhaseGraphSpecification specification{
        .phases = {phase("", ""), phase("alpha", invalid), phase("beta", "valid"), phase("gamma", "valid"),
                   phase("dup", "one"), phase("dup", "two"), phase(invalid, "valid")},
        .interfaces =
            {
                interface("", "alpha", "beta", ""),
                interface("ambiguous-endpoint", "dup", "alpha", "valid"),
                interface("bad-endpoints", invalid, invalid, "valid"),
                interface(invalid, "alpha", "beta", invalid),
                interface("dup-i", "alpha", "beta", "valid"),
                interface("dup-i", "alpha", "gamma", "valid"),
                interface("missing", "absent", "alpha", "valid"),
                interface("missing-plus", "alpha", "absent", "valid"),
                interface("invalid-plus-only", "alpha", invalid, "valid"),
                interface("pair-a", "beta", "gamma", "valid"),
                interface("pair-b", "gamma", "beta", "valid"),
                interface("self", "alpha", "alpha", "valid"),
            },
    };
    permute_for_rank(specification.phases, rank);
    permute_for_rank(specification.interfaces, rank);
    return specification;
}

void test_collected_field_and_topology_errors(rift::RiftContext& context)
{
    using namespace boost::ut;

    std::size_t compatibility_calls = 0;
    const auto result = context.create_phase_graph(
        invalid_specification(context.this_mpi_process()),
        [&compatibility_calls](const auto&, const auto&, const auto&) -> rift::InterfaceCompatibilityDecision {
            ++compatibility_calls;
            return {};
        });

    expect(not result.has_value());
    if (result.has_value()) {
        return;
    }

    const auto& errors = result.error();
    expect(errors.size() == std::size_t{18});
    expect(error_count(errors, rift::PhaseGraphErrorCode::empty_phase_name) == std::size_t{1});
    expect(error_count(errors, rift::PhaseGraphErrorCode::empty_physics_key) == std::size_t{1});
    expect(error_count(errors, rift::PhaseGraphErrorCode::duplicate_phase_name) == std::size_t{1});
    expect(error_count(errors, rift::PhaseGraphErrorCode::empty_interface_name) == std::size_t{1});
    expect(error_count(errors, rift::PhaseGraphErrorCode::empty_interface_operator_key) == std::size_t{1});
    expect(error_count(errors, rift::PhaseGraphErrorCode::duplicate_interface_name) == std::size_t{1});
    expect(error_count(errors, rift::PhaseGraphErrorCode::missing_incident_phase) == std::size_t{2});
    expect(error_count(errors, rift::PhaseGraphErrorCode::self_interface) == std::size_t{1});
    expect(error_count(errors, rift::PhaseGraphErrorCode::duplicate_phase_pair) == std::size_t{2});
    expect(error_count(errors, rift::PhaseGraphErrorCode::invalid_utf8) == std::size_t{7});
    expect(compatibility_calls == std::size_t{0});

    const auto phase_error = std::ranges::find_if(
        errors, [](const auto& error) { return error.code == rift::PhaseGraphErrorCode::empty_phase_name; });
    expect(phase_error != errors.end());
    if (phase_error != errors.end()) {
        expect(phase_error->subject.has_value());
        if (phase_error->subject.has_value()) {
            const auto* subject = std::get_if<rift::PhaseErrorSubject>(&*phase_error->subject);
            expect(subject != nullptr);
            if (subject != nullptr) {
                expect(subject->sorted_index == std::size_t{0});
                expect(subject->specification.name.empty());
                expect(subject->specification.physics_key.value().empty());
            }
        }
    }

    const auto interface_error = std::ranges::find_if(
        errors, [](const auto& error) { return error.code == rift::PhaseGraphErrorCode::empty_interface_name; });
    expect(interface_error != errors.end());
    if (interface_error != errors.end()) {
        expect(interface_error->subject.has_value());
        if (interface_error->subject.has_value()) {
            const auto* subject = std::get_if<rift::InterfaceErrorSubject>(&*interface_error->subject);
            expect(subject != nullptr);
            if (subject != nullptr) {
                expect(subject->sorted_index == std::size_t{0});
                expect(subject->specification.name.empty());
                expect(subject->specification.minus_phase == "alpha");
                expect(subject->specification.plus_phase == "beta");
                expect(subject->specification.operator_key.value().empty());
            }
        }
    }
}

void test_missing_compatibility(rift::RiftContext& context)
{
    const auto result = context.create_phase_graph(single_interface_specification(), {});
    expect_single_error(result, rift::PhaseGraphErrorCode::missing_compatibility_check,
                        {"compatibility test", "interfaces"});
}

void test_input_mismatch(rift::RiftContext& context)
{
    using namespace boost::ut;

    auto specification = triangle_specification(context.this_mpi_process());
    if (context.this_mpi_process() == 1) {
        const auto phase_position =
            std::ranges::find_if(specification.phases, [](const auto& item) { return item.name == "air"; });
        phase_position->physics_key = rift::PhysicsKey{"calorically-perfect-gas"};
    }

    std::size_t compatibility_calls = 0;
    const auto result = context.create_phase_graph(
        std::move(specification),
        [&compatibility_calls](const auto&, const auto&, const auto&) -> rift::InterfaceCompatibilityDecision {
            ++compatibility_calls;
            return {};
        });

    expect_single_error(result, rift::PhaseGraphErrorCode::collective_input_mismatch, {"rank 1", "rank 0"});
    expect(compatibility_calls == std::size_t{0});
}

void test_compatible_failures(rift::RiftContext& context)
{
    using namespace boost::ut;

    std::vector<std::string> observations;
    const auto result = context.create_phase_graph(
        triangle_specification(context.this_mpi_process()),
        [&observations](const auto& minus_phase, const auto& plus_phase,
                        const auto& interface) -> rift::InterfaceCompatibilityDecision {
            observations.push_back(compatibility_observation(minus_phase, plus_phase, interface));
            if (interface.name == "air-solid") {
                return std::unexpected(std::string{"contact model is unavailable"});
            }
            if (interface.name == "free-surface") {
                throw std::runtime_error{"surface registry failed"};
            }
            return {};
        });

    expect(not result.has_value());
    if (result.has_value()) {
        return;
    }

    expect(observations.size() == std::size_t{3});
    const auto& errors = result.error();
    expect(errors.size() == std::size_t{2});
    if (errors.size() != 2) {
        return;
    }

    const auto& rejection = errors.at(0);
    expect(rejection.code == rift::PhaseGraphErrorCode::incompatible_interface);
    expect_message_contains(
        rejection, {"contact model is unavailable", "air-solid", "contact", "solid", "elastic", "air", "ideal-gas"});
    expect(rejection.subject.has_value());
    if (rejection.subject.has_value()) {
        const auto* subject = std::get_if<rift::InterfaceErrorSubject>(&*rejection.subject);
        expect(subject != nullptr);
        if (subject != nullptr) {
            expect(subject->sorted_index == std::size_t{0});
            expect(subject->specification.name == "air-solid");
        }
    }

    const auto& exception = errors.at(1);
    expect(exception.code == rift::PhaseGraphErrorCode::compatibility_test_exception);
    expect_message_contains(exception, {"surface registry failed", "free-surface", "surface-tension", "water",
                                        "incompressible", "air", "ideal-gas"});
    expect(exception.subject.has_value());
    if (exception.subject.has_value()) {
        const auto* subject = std::get_if<rift::InterfaceErrorSubject>(&*exception.subject);
        expect(subject != nullptr);
        if (subject != nullptr) {
            expect(subject->sorted_index == std::size_t{1});
            expect(subject->specification.name == "free-surface");
        }
    }
}

void test_nonstandard_exception(rift::RiftContext& context)
{
    const auto result = context.create_phase_graph(
        single_interface_specification(),
        [](const auto&, const auto&, const auto&) -> rift::InterfaceCompatibilityDecision { throw 7; });

    expect_single_error(
        result, rift::PhaseGraphErrorCode::compatibility_test_exception,
        {"free-surface", "surface-tension", "liquid", "incompressible", "gas", "ideal-gas", "non-standard exception"});
}

void test_compatibility_mismatch(rift::RiftContext& context)
{
    using namespace boost::ut;

    std::size_t compatibility_calls = 0;
    const auto result = context.create_phase_graph(
        triangle_specification(context.this_mpi_process()),
        [&context, &compatibility_calls](const auto&, const auto&,
                                         const auto& interface) -> rift::InterfaceCompatibilityDecision {
            ++compatibility_calls;
            if (context.this_mpi_process() == 1 && interface.name == "air-solid") {
                return std::unexpected(std::string{"rank-local rejection"});
            }
            return {};
        });

    expect_single_error(result, rift::PhaseGraphErrorCode::collective_compatibility_mismatch, {"rank 1", "rank 0"});
    expect(compatibility_calls == std::size_t{3});
}

void run_scenario(rift::RiftContext& context, const std::string_view scenario)
{
    using namespace boost::ut;

    if (scenario == "success") {
        test_successful_agreement(context);
    }
    else if (scenario == "no_interfaces") {
        test_graph_without_interfaces(context);
    }
    else if (scenario == "accept_all_policy") {
        test_accept_all_policy(context);
    }
    else if (scenario == "no_phases") {
        test_no_phases_and_sealed_failure(context);
    }
    else if (scenario == "field_errors") {
        test_collected_field_and_topology_errors(context);
    }
    else if (scenario == "missing_compatibility") {
        test_missing_compatibility(context);
    }
    else if (scenario == "input_mismatch") {
        test_input_mismatch(context);
    }
    else if (scenario == "compatibility_failures") {
        test_compatible_failures(context);
    }
    else if (scenario == "nonstandard_exception") {
        test_nonstandard_exception(context);
    }
    else if (scenario == "compatibility_mismatch") {
        test_compatibility_mismatch(context);
    }
    else {
        expect(false) << "unknown phase-graph agreement scenario: " << scenario;
    }
}

} // namespace

int main(int argc, char** argv)
{
    using namespace boost::ut;

    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): use the conventional argc/argv contract.
    const std::string selected_scenario = argc > 1 ? argv[1] : "success";
    rift::RiftContext actual_context(argc, argv);

    // Work around Boost.UT suite capture restrictions while keeping one live context.
    static rift::RiftContext* context_ptr = nullptr;
    static std::string scenario;
    context_ptr = &actual_context;
    scenario = selected_scenario;

    [[maybe_unused]] const suite<"Phase graph agreement"> suite = [] {
        auto& context = *context_ptr;
        "the selected collective phase-graph scenario"_test = [&context] { run_scenario(context, scenario); };
    };

    const auto result = static_cast<int>(cfg<>.run());
    context_ptr = nullptr;
    return result;
}
