#pragma once

#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/phase_graph.hpp>

namespace rift_test::phase_graph_orientation_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "edge orientation does not depend on phase insertion order"_test = [] {
        const auto run = rift::test::make_test_run();
        const auto first =
            rift::make_phase_graph(run, {{"liquid", "low-mach"}, {"gas", "compressible"}},
                                   {{"surface", "liquid", "gas", "finite-rate"}}, rift::test::accept_all_interfaces);
        const auto second =
            rift::make_phase_graph(run, {{"gas", "compressible"}, {"liquid", "low-mach"}},
                                   {{"surface", "liquid", "gas", "finite-rate"}}, rift::test::accept_all_interfaces);

        expect(first.has_value() && second.has_value());
        const auto& first_edge = first->interfaces().front();
        const auto& second_edge = second->interfaces().front();
        expect(first->phase(first_edge.minus_phase).name == "liquid");
        expect(first->phase(first_edge.plus_phase).name == "gas");
        expect(first_edge.minus_phase == second_edge.minus_phase);
        expect(first_edge.plus_phase == second_edge.plus_phase);
    };
}

} // namespace rift_test::phase_graph_orientation_00
