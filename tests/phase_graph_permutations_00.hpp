#pragma once

#include "phase_graph_test_support.hpp"

#include <array>
#include <boost/ut.hpp>
#include <cstddef>
#include <deal.II/base/mpi.h>
#include <optional>
#include <rift/phase_graph.hpp>
#include <string>
#include <vector>

namespace rift_test::phase_graph_permutations_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "every three-phase cycle declaration permutation has one canonical graph"_test = [] {
        const std::array phases{rift::PhaseSpecification{"alpha", "pa"}, rift::PhaseSpecification{"beta", "pb"},
                                rift::PhaseSpecification{"gamma", "pg"}};
        const std::array interfaces{rift::InterfaceSpecification{"ab", "alpha", "beta", "lab"},
                                    rift::InterfaceSpecification{"bc", "beta", "gamma", "lbc"},
                                    rift::InterfaceSpecification{"ca", "gamma", "alpha", "lca"}};
        const std::string expected_json =
            R"({"schema":"rift.phase_graph","version":1,"phases":[{"id":0,"name":"alpha","physics":"pa"},{"id":1,"name":"beta","physics":"pb"},{"id":2,"name":"gamma","physics":"pg"}],"interfaces":[{"id":0,"name":"ab","minus_phase":0,"plus_phase":1,"operator":"lab"},{"id":1,"name":"bc","minus_phase":1,"plus_phase":2,"operator":"lbc"},{"id":2,"name":"ca","minus_phase":2,"plus_phase":0,"operator":"lca"}]})";
        const std::vector<std::string> expected_trace{"ab:alpha>beta", "bc:beta>gamma", "ca:gamma>alpha"};
        const auto run = rift::test::make_test_run();

        constexpr std::array permutations{
            std::array{0, 1, 2}, std::array{0, 2, 1}, std::array{1, 0, 2},
            std::array{1, 2, 0}, std::array{2, 0, 1}, std::array{2, 1, 0},
        };
        for (const auto& phase_order : permutations) {
            for (const auto& interface_order : permutations) {
                std::vector<rift::PhaseSpecification> configured_phases;
                std::vector<rift::InterfaceSpecification> configured_interfaces;
                configured_phases.reserve(phases.size());
                configured_interfaces.reserve(interfaces.size());
                for (const int index : phase_order) {
                    configured_phases.push_back(phases.at(static_cast<std::size_t>(index)));
                }
                for (const int index : interface_order) {
                    configured_interfaces.push_back(interfaces.at(static_cast<std::size_t>(index)));
                }

                std::vector<std::string> trace;
                const auto graph = rift::make_phase_graph(
                    run, configured_phases, configured_interfaces,
                    [&trace](const auto& minus, const auto& plus, const auto& interface) -> std::optional<std::string> {
                        trace.push_back(interface.name + ":" + minus.name + ">" + plus.name);
                        return std::nullopt;
                    });

                expect(graph.has_value());
                expect(graph->canonical_json() == expected_json);
                expect(trace == expected_trace);
                expect(graph->find_phase("alpha") == rift::PhaseId::from_index(0));
                expect(graph->find_interface("ca") == rift::InterfaceId::from_index(2));
                const auto ca = graph->material_interface(rift::InterfaceId::from_index(2));
                expect(ca.minus_phase == rift::PhaseId::from_index(2));
                expect(ca.plus_phase == rift::PhaseId::from_index(0));
                expect(graph->find_interface(ca.plus_phase, ca.minus_phase) == ca.id);
            }
        }
    };
}

} // namespace rift_test::phase_graph_permutations_00
