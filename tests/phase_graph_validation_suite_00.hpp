#pragma once

#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <optional>
#include <rift/phase_graph.hpp>
#include <string>

namespace rift_test::phase_graph_validation_suite_00 {

inline rift::PhaseGraphResult make_duplicate_pair(const std::string& second_law)
{
    return rift::make_phase_graph(rift::test::make_test_run(), {{"a", "pa"}, {"b", "pb"}},
                                  {{"first", "a", "b", "law"}, {"second", "b", "a", second_law}},
                                  rift::test::accept_all_interfaces);
}

} // namespace rift_test::phase_graph_validation_suite_00

namespace rift_test::phase_graph_validation_suite_00 {

inline void register_tests()
{

    using namespace boost::ut;

    "phase graph phase specification validation"_test = [] {
        using namespace boost::ut;

        "a runtime graph must contain at least one phase"_test = [] {
            const auto run = rift::test::make_test_run();
            const auto result = rift::make_phase_graph(run, {}, {});

            expect(!result.has_value());
            expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::no_phases));
        };

        "phase names may not be empty"_test = [] {
            const auto run = rift::test::make_test_run();
            const auto result = rift::make_phase_graph(run, {{"gas", "compressible"}, {"", "low-mach"}}, {});

            expect(!result.has_value());
            expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::empty_phase_name));
            expect(result.error().front().message.contains("canonical index 0"));
        };

        "phase physics keys may not be empty"_test = [] {
            const auto run = rift::test::make_test_run();
            const auto result = rift::make_phase_graph(run, {{"gas", ""}}, {});

            expect(!result.has_value());
            expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::empty_physics_key));
            expect(result.error().front().message.contains("gas"));
        };

        "duplicate phase names are rejected"_test = [] {
            const auto run = rift::test::make_test_run();
            const auto result = rift::make_phase_graph(run, {{"gas", "compressible"}, {"gas", "low-mach"}}, {});

            expect(!result.has_value());
            expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::duplicate_phase_name));
            expect(result.error().front().message.contains("gas"));
        };
    };

    "phase graph interface specification validation"_test = [] {
        using namespace boost::ut;

        "interface names may not be empty"_test = [] {
            const auto run = rift::test::make_test_run();
            const auto result =
                rift::make_phase_graph(run, {{"gas", "compressible"}, {"liquid", "low-mach"}},
                                       {{"", "liquid", "gas", "finite-rate"}}, rift::test::accept_all_interfaces);

            expect(!result.has_value());
            expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::empty_interface_name));
            expect(result.error().front().message.contains("canonical index 0"));
        };

        "interface operator keys may not be empty"_test = [] {
            const auto run = rift::test::make_test_run();
            const auto result =
                rift::make_phase_graph(run, {{"gas", "compressible"}, {"liquid", "low-mach"}},
                                       {{"surface", "liquid", "gas", ""}}, rift::test::accept_all_interfaces);

            expect(!result.has_value());
            expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::empty_interface_operator_key));
            expect(result.error().front().message.contains("surface"));
        };

        "an interface must name two phases in the graph"_test = [] {
            const auto run = rift::test::make_test_run();
            const auto result =
                rift::make_phase_graph(run, {{"gas", "compressible"}}, {{"surface", "liquid", "gas", "finite-rate"}},
                                       rift::test::accept_all_interfaces);

            expect(!result.has_value());
            expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::missing_incident_phase));
            expect(result.error().front().message.contains("liquid"));
            expect(result.error().front().message.contains("surface"));
        };

        "an interface reports a missing plus phase"_test = [] {
            const auto run = rift::test::make_test_run();
            const auto result =
                rift::make_phase_graph(run, {{"gas", "compressible"}}, {{"surface", "gas", "liquid", "finite-rate"}},
                                       rift::test::accept_all_interfaces);

            expect(!result.has_value());
            expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::missing_incident_phase));
            expect(result.error().front().message.contains("plus phase 'liquid'"));
        };

        "an interface may not join a phase to itself"_test = [] {
            const auto run = rift::test::make_test_run();
            const auto result =
                rift::make_phase_graph(run, {{"gas", "compressible"}}, {{"invalid", "gas", "gas", "finite-rate"}},
                                       rift::test::accept_all_interfaces);

            expect(!result.has_value());
            expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::self_interface));
        };

        "duplicate interface names are rejected"_test = [] {
            const auto run = rift::test::make_test_run();
            const auto result = rift::make_phase_graph(run, {{"a", "fluid"}, {"b", "fluid"}, {"c", "solid"}},
                                                       {{"contact", "a", "b", "law"}, {"contact", "b", "c", "law"}},
                                                       rift::test::accept_all_interfaces);

            expect(!result.has_value());
            expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::duplicate_interface_name));
        };

        "the initial graph has at most one edge per unordered phase pair"_test = [] {
            const auto run = rift::test::make_test_run();
            const auto result =
                rift::make_phase_graph(run, {{"gas", "compressible"}, {"liquid", "low-mach"}},
                                       {{"first", "gas", "liquid", "law-a"}, {"second", "liquid", "gas", "law-b"}},
                                       rift::test::accept_all_interfaces);

            expect(!result.has_value());
            expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::duplicate_phase_pair));
        };

        "equal law keys do not turn one pair into two edges"_test = [] {
            const auto graph = make_duplicate_pair("law");
            expect(!graph.has_value());
            expect(rift::test::has_error(graph.error(), rift::PhaseGraphErrorCode::duplicate_phase_pair));
        };

        "unequal law keys remain one conflicting composite-law pair"_test = [] {
            const auto graph = make_duplicate_pair("other-law");
            expect(!graph.has_value());
            expect(rift::test::has_error(graph.error(), rift::PhaseGraphErrorCode::duplicate_phase_pair));
        };
    };

    "phase graph compatibility validation"_test = [] {
        using namespace boost::ut;

        "an interface graph requires an explicit compatibility check"_test = [] {
            const auto run = rift::test::make_test_run();
            const auto result = rift::make_phase_graph(run, {{"gas", "compressible"}, {"liquid", "low-mach"}},
                                                       {{"surface", "liquid", "gas", "finite-rate"}});

            expect(!result.has_value());
            expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::missing_compatibility_check));
        };

        "every supplied interface requires a compatibility callback"_test = [] {
            const auto run = rift::test::make_test_run();
            const auto result =
                rift::make_phase_graph(run, {{"gas", "compressible"}}, {{"surface", "missing", "gas", "law"}});

            expect(!result.has_value());
            expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::missing_incident_phase));
            expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::missing_compatibility_check));
        };

        "a construction-time validator rejects incompatible interface models"_test = [] {
            const auto run = rift::test::make_test_run();
            const rift::InterfaceCompatibilityCheck reject =
                [](const rift::PhaseDescriptor&, const rift::PhaseDescriptor&, const rift::InterfaceSpecification&) {
                    return std::optional<std::string>{"no compiled kernel supports this phase pairing"};
                };
            const auto result = rift::make_phase_graph(run, {{"solid", "thermomechanical"}, {"gas", "compressible"}},
                                                       {{"surface", "solid", "gas", "finite-rate"}}, reject);

            expect(!result.has_value());
            expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::incompatible_interface));
            expect(result.error().front().message.contains("surface"));
            expect(result.error().front().message.contains("solid"));
            expect(result.error().front().message.contains("gas"));
        };
    };
}

} // namespace rift_test::phase_graph_validation_suite_00
