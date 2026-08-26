#pragma once

#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/phase_graph.hpp>
#include <string>

namespace rift_test::phase_graph_json_controls_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "canonical JSON escapes every ASCII control byte"_test = [] {
        std::string controls;
        for (unsigned char value = 0; value < 0x20U; ++value) {
            controls.push_back(static_cast<char>(value));
        }
        const std::string expected = "\\u0000\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007"
                                     "\\b\\t\\n\\u000b\\f\\r\\u000e\\u000f"
                                     "\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015\\u0016\\u0017"
                                     "\\u0018\\u0019\\u001a\\u001b\\u001c\\u001d\\u001e\\u001f";

        const auto graph = rift::make_phase_graph(rift::test::make_test_run(), {{controls, "physics"}}, {});

        expect(graph.has_value());
        const std::string expected_field = R"("name":")" + expected + '"';
        expect(graph->canonical_json().contains(expected_field));
    };
}

} // namespace rift_test::phase_graph_json_controls_00
