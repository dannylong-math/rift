#include <boost/ut.hpp>
#include <rift/phase_graph.hpp>
#include <string>
#include <utility>

namespace {

[[nodiscard]] rift::PhaseGraphError phase_error(const rift::PhaseGraphErrorCode code, std::string message,
                                                const rift::PhaseGraphErrorSubject& subject)
{
    return {.code = code, .message = std::move(message), .subject = subject};
}

[[nodiscard]] rift::PhaseGraphError interface_error(const rift::PhaseGraphErrorCode code, std::string message,
                                                    const rift::PhaseGraphErrorSubject& subject)
{
    return {.code = code, .message = std::move(message), .subject = subject};
}

} // namespace

int main()
{
    using namespace boost::ut;

    const suite<"Phase graph error formatting"> suite = [] {
        "an empty error range produces no text"_test = [] { expect(rift::format_phase_graph_errors({}).empty()); };

        "an unscoped error remains one standalone diagnostic"_test = [] {
            const rift::PhaseGraphErrors errors{{.code = rift::PhaseGraphErrorCode::no_phases,
                                                 .message = "a phase graph must contain at least one phase"}};

            expect(rift::format_phase_graph_errors(errors) ==
                   "[no_phases] a phase graph must contain at least one phase");
        };

        "phase errors share one configured-versus-expected table"_test = [] {
            const rift::PhaseGraphErrorSubject subject = rift::PhaseErrorSubject{
                .sorted_index = 2,
                .specification = {.name = "water\n\"phase", .physics_key = rift::PhysicsKey{""}},
            };
            const rift::PhaseGraphErrors errors{
                phase_error(rift::PhaseGraphErrorCode::empty_physics_key, "the physics key is empty", subject),
                phase_error(rift::PhaseGraphErrorCode::invalid_utf8, "the phase name contains unsafe text", subject),
            };

            const std::string expected = "Phase \"water\\n\\\"phase\"\n"
                                         "\n"
                                         "Field    Configured        Expected\n"
                                         "------------------------------------------------------------\n"
                                         "Name     \"water\\n\\\"phase\"  non-empty unique UTF-8 string\n"
                                         "Physics  \"\"                non-empty UTF-8 phase-physics key\n"
                                         "\n"
                                         "Errors\n"
                                         "  [empty_physics_key] the physics key is empty\n"
                                         "  [invalid_utf8] the phase name contains unsafe text";
            expect(rift::format_phase_graph_errors(errors) == expected);
        };

        "interface fields escape controls invalid UTF-8 and punctuation"_test = [] {
            std::string invalid_operator{"law"};
            invalid_operator.push_back(static_cast<char>(0xC3));
            invalid_operator.push_back('(');
            invalid_operator.push_back(static_cast<char>(0x7F));

            const rift::PhaseGraphErrorSubject subject = rift::InterfaceErrorSubject{
                .sorted_index = 4,
                .specification = {.name = "free\t\\\"surface",
                                  .minus_phase = "liquid\rphase",
                                  .plus_phase = "gas\bphase",
                                  .operator_key = rift::InterfaceOperatorKey{std::move(invalid_operator)}},
            };
            const rift::PhaseGraphErrors errors{interface_error(rift::PhaseGraphErrorCode::invalid_utf8,
                                                                "the operator key is not valid UTF-8", subject)};
            const auto formatted = rift::format_phase_graph_errors(errors);

            expect(formatted.contains(R"(Interface "free\t\\\"surface")"));
            expect(formatted.contains(R"("liquid\rphase")"));
            expect(formatted.contains(R"("gas\bphase")"));
            expect(formatted.contains(R"("law\xC3(\x7F")"));
            expect(formatted.contains("compatible non-empty UTF-8 interface-operator key"));
            expect(formatted.ends_with("[invalid_utf8] the operator key is not valid UTF-8"));
        };

        "empty names use their canonical position only as a fallback"_test = [] {
            const rift::PhaseGraphErrorSubject phase_subject = rift::PhaseErrorSubject{
                .sorted_index = 1,
                .specification = {.name = "", .physics_key = rift::PhysicsKey{"euler"}},
            };
            const rift::PhaseGraphErrorSubject interface_subject = rift::InterfaceErrorSubject{
                .sorted_index = 3,
                .specification = {.name = "",
                                  .minus_phase = "liquid",
                                  .plus_phase = "gas",
                                  .operator_key = rift::InterfaceOperatorKey{"flux"}},
            };
            const rift::PhaseGraphErrors errors{
                phase_error(rift::PhaseGraphErrorCode::empty_phase_name, "phase name is empty", phase_subject),
                interface_error(rift::PhaseGraphErrorCode::empty_interface_name, "interface name is empty",
                                interface_subject),
            };
            const auto formatted = rift::format_phase_graph_errors(errors);

            expect(formatted.contains("Phase with empty name (canonical position 1)"));
            expect(formatted.contains("Interface with empty name (canonical position 3)"));
        };

        "separated errors with the same subject are grouped at first occurrence"_test = [] {
            const rift::PhaseGraphErrorSubject subject = rift::PhaseErrorSubject{
                .sorted_index = 0,
                .specification = {.name = "water", .physics_key = rift::PhysicsKey{"incompressible"}},
            };
            const rift::PhaseGraphErrors errors{
                phase_error(rift::PhaseGraphErrorCode::empty_phase_name, "first subject error", subject),
                {.code = rift::PhaseGraphErrorCode::duplicate_phase_name, .message = "unscoped error"},
                phase_error(rift::PhaseGraphErrorCode::empty_physics_key, "second subject error", subject),
            };
            const auto formatted = rift::format_phase_graph_errors(errors);

            expect(formatted.find("first subject error") < formatted.find("second subject error"));
            expect(formatted.find("second subject error") < formatted.find("unscoped error"));
            expect(formatted.find("Phase \"water\"") == formatted.rfind("Phase \"water\""));
        };
    };

    return static_cast<int>(cfg<>.run());
}
