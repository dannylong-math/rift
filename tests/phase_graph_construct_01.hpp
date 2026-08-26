#pragma once

#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/phase_graph.hpp>

namespace rift_test::phase_graph_construct_01 {

inline void register_tests()
{
    using namespace boost::ut;

    "a graph may contain more than two phases"_test = [] {
        const auto run = rift::test::make_test_run();
        const auto result = rift::make_phase_graph(
            run, {{"solid", "thermomechanical"}, {"liquid", "low-mach"}, {"gas", "compressible"}},
            {{"evaporation", "liquid", "gas", "finite-rate"}, {"melting", "solid", "liquid", "stefan"}},
            rift::test::accept_all_interfaces);

        expect(result.has_value());
        expect(result->phases().size() == 3_u);
        expect(result->interfaces().size() == 2_u);
    };
}

} // namespace rift_test::phase_graph_construct_01
