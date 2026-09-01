#include <boost/ut.hpp>
#include <compare>
#include <concepts>
#include <cstdint>
#include <rift/phase_graph.hpp>
#include <string>
#include <string_view>

namespace {

template<class Left, class Right>
concept EqualityComparableWith = requires(const Left& left, const Right& right) {
    { left == right } -> std::same_as<bool>;
};

template<class Left, class Right>
concept ThreeWayComparableWith = requires(const Left& left, const Right& right) { left <=> right; };

static_assert(not std::same_as<rift::PhaseId, rift::InterfaceId>);
static_assert(not std::convertible_to<rift::PhaseId, rift::InterfaceId>);
static_assert(not std::convertible_to<rift::InterfaceId, rift::PhaseId>);
static_assert(not EqualityComparableWith<rift::PhaseId, rift::InterfaceId>);
static_assert(not ThreeWayComparableWith<rift::PhaseId, rift::InterfaceId>);

static_assert(not std::same_as<rift::PhysicsKey, rift::InterfaceOperatorKey>);
static_assert(not std::convertible_to<rift::PhysicsKey, rift::InterfaceOperatorKey>);
static_assert(not std::convertible_to<rift::InterfaceOperatorKey, rift::PhysicsKey>);

} // namespace

int main()
{
    using namespace boost::ut;

    suite<"Phase graph"> suite = [] {
        "phase and interface IDs preserve zero-based values and ordering"_test = [] {
            constexpr auto first_phase = rift::PhaseId::from_index(0);
            constexpr auto second_phase = rift::PhaseId::from_index(1);
            constexpr auto first_interface = rift::InterfaceId::from_index(0);

            static_assert(first_phase.value() == std::uint32_t{0});
            static_assert(second_phase.value() == std::uint32_t{1});
            static_assert(first_interface.value() == std::uint32_t{0});
            static_assert(first_phase < second_phase);

            expect(first_phase.value() == std::uint32_t{0});
            expect(second_phase.value() == std::uint32_t{1});
            expect(first_interface.value() == std::uint32_t{0});
            expect(first_phase == rift::PhaseId::from_index(0));
            expect(first_phase < second_phase);
            expect(second_phase > first_phase);
            expect((first_phase <=> rift::PhaseId::from_index(0)) == std::strong_ordering::equal);
        };

        "registry keys own exact spellings and remain type distinct"_test = [] {
            std::string physics_spelling = "Navier Stokes/v1";
            std::string operator_spelling = "Sharp-Flux::V2";
            const rift::PhysicsKey physics_key{physics_spelling};
            const rift::InterfaceOperatorKey operator_key{operator_spelling};

            physics_spelling.assign("changed");
            operator_spelling.clear();

            expect(physics_key.value() == std::string_view{"Navier Stokes/v1"});
            expect(operator_key.value() == std::string_view{"Sharp-Flux::V2"});
            expect(physics_key == rift::PhysicsKey{"Navier Stokes/v1"});
            expect(operator_key == rift::InterfaceOperatorKey{"Sharp-Flux::V2"});
            expect(not std::same_as<rift::PhysicsKey, rift::InterfaceOperatorKey>);
        };

        "phase specifications own their inputs and permit empty values"_test = [] {
            std::string phase_name = "liquid";
            std::string physics_spelling = "incompressible";
            const rift::PhaseSpecification phase{.name = phase_name, .physics_key = rift::PhysicsKey{physics_spelling}};

            phase_name.assign("changed");
            physics_spelling.assign("changed");

            expect(phase.name == "liquid");
            expect(phase.physics_key.value() == std::string_view{"incompressible"});

            const rift::PhaseSpecification empty_phase{.name = "", .physics_key = rift::PhysicsKey{""}};
            expect(empty_phase.name.empty());
            expect(empty_phase.physics_key.value().empty());
        };

        "interface specifications own inputs and preserve declared orientation"_test = [] {
            std::string interface_name = "free-surface";
            std::string minus_phase = "liquid";
            std::string plus_phase = "gas";
            std::string operator_spelling = "surface-tension";
            const rift::InterfaceSpecification interface{
                .name = interface_name,
                .minus_phase = minus_phase,
                .plus_phase = plus_phase,
                .operator_key = rift::InterfaceOperatorKey{operator_spelling},
            };

            interface_name.clear();
            minus_phase.assign("changed");
            plus_phase.assign("changed");
            operator_spelling.assign("changed");

            expect(interface.name == "free-surface");
            expect(interface.minus_phase == "liquid");
            expect(interface.plus_phase == "gas");
            expect(interface.operator_key.value() == std::string_view{"surface-tension"});

            const rift::InterfaceSpecification empty_interface{
                .name = "",
                .minus_phase = "",
                .plus_phase = "",
                .operator_key = rift::InterfaceOperatorKey{""},
            };
            expect(empty_interface.name.empty());
            expect(empty_interface.minus_phase.empty());
            expect(empty_interface.plus_phase.empty());
            expect(empty_interface.operator_key.value().empty());
        };

        "phase graph diagnostics collect local and collective failures"_test = [] {
            const rift::PhaseGraphErrors errors{
                {.code = rift::PhaseGraphErrorCode::empty_phase_name, .message = "phase name is empty"},
                {.code = rift::PhaseGraphErrorCode::collective_input_mismatch, .message = "rank inputs differ"},
                {.code = rift::PhaseGraphErrorCode::collective_compatibility_mismatch,
                 .message = "rank outcomes differ"},
            };

            expect(errors.size() == std::size_t{3});
            expect(errors[0].code == rift::PhaseGraphErrorCode::empty_phase_name);
            expect(errors[0].message == "phase name is empty");
            expect(errors[1].code == rift::PhaseGraphErrorCode::collective_input_mismatch);
            expect(errors[1].message == "rank inputs differ");
            expect(errors[2].code == rift::PhaseGraphErrorCode::collective_compatibility_mismatch);
            expect(errors[2].message == "rank outcomes differ");
        };
    };

    return static_cast<int>(cfg<>.run());
}
