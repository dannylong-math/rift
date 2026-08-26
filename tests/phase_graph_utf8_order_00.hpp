#pragma once

#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <optional>
#include <rift/phase_graph.hpp>
#include <string>
#include <vector>

namespace rift_test::phase_graph_utf8_order_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "UTF-8 names use unsigned-byte canonical ID and callback order"_test = [] {
        const std::string ascii_boundary{"\x7f", 1};
        const std::string two_byte_min{"\xc2\x80", 2};
        const std::string two_byte_max{"\xdf\xbf", 2};
        std::vector<std::string> callback_trace;
        const auto graph = rift::make_phase_graph(
            rift::test::make_test_run(),
            {{two_byte_max, "p-max"}, {two_byte_min, "p-min"}, {ascii_boundary, "p-ascii"}},
            {{two_byte_min, ascii_boundary, two_byte_min, "law-2"},
             {ascii_boundary, two_byte_min, two_byte_max, "law-1"}},
            [&callback_trace](const auto&, const auto&, const auto& interface) -> std::optional<std::string> {
                callback_trace.push_back(interface.name);
                return std::nullopt;
            });

        expect(graph.has_value());
        expect(graph->find_phase(ascii_boundary) == rift::PhaseId::from_index(0));
        expect(graph->find_phase(two_byte_min) == rift::PhaseId::from_index(1));
        expect(graph->find_phase(two_byte_max) == rift::PhaseId::from_index(2));
        expect(callback_trace == std::vector<std::string>{ascii_boundary, two_byte_min});
        expect(graph->find_interface(ascii_boundary) == rift::InterfaceId::from_index(0));
        expect(graph->find_interface(two_byte_min) == rift::InterfaceId::from_index(1));
    };
}

} // namespace rift_test::phase_graph_utf8_order_00
