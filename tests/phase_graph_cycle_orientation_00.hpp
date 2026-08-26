#pragma once

#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/phase_graph.hpp>

namespace rift_test::phase_graph_cycle_orientation_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "a three-phase cycle preserves each declared orientation"_test = [] {
        const auto graph =
            rift::make_phase_graph(rift::test::make_test_run(), {{"a", "pa"}, {"b", "pb"}, {"c", "pc"}},
                                   {{"ab", "b", "a", "lab"}, {"bc", "c", "b", "lbc"}, {"ca", "a", "c", "lca"}},
                                   rift::test::accept_all_interfaces);

        expect(graph.has_value());
        const auto ab = graph->material_interface(graph->find_interface("ab").value());
        expect(graph->phase(ab.minus_phase).name == "b");
        expect(graph->phase(ab.plus_phase).name == "a");
        expect(graph->find_interface(ab.plus_phase, ab.minus_phase) == ab.id);
        expect(graph->interfaces().size() == 3_u);
    };
}

} // namespace rift_test::phase_graph_cycle_orientation_00
