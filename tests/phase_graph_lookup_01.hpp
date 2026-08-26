#pragma once

#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/phase_graph.hpp>

namespace rift_test::phase_graph_lookup_01 {

inline void register_tests()
{
    using namespace boost::ut;

    "an interface resolves from its unordered incident phase pair"_test = [] {
        const auto run = rift::test::make_test_run();
        const auto result =
            rift::make_phase_graph(run, {{"gas", "compressible"}, {"liquid", "low-mach"}},
                                   {{"surface", "liquid", "gas", "finite-rate"}}, rift::test::accept_all_interfaces);

        expect(result.has_value());
        const auto gas = result->find_phase("gas");
        const auto liquid = result->find_phase("liquid");
        expect(gas.has_value() && liquid.has_value());
        expect(result->find_interface(*gas, *liquid).has_value());
        expect(result->find_interface(*liquid, *gas).has_value());
    };
}

} // namespace rift_test::phase_graph_lookup_01
