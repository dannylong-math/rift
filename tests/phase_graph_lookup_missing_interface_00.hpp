#pragma once

#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/phase_graph.hpp>

namespace rift_test::phase_graph_lookup_missing_interface_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "missing interfaces do not resolve by name or phase pair"_test = [] {
        const auto run = rift::test::make_test_run();
        const auto result =
            rift::make_phase_graph(run, {{"gas", "compressible"}, {"liquid", "low-mach"}, {"solid", "elastic"}},
                                   {{"surface", "liquid", "gas", "finite-rate"}}, rift::test::accept_all_interfaces);

        expect(result.has_value());
        expect(!result->find_interface("absent").has_value());
        expect(!result->find_interface("aardvark").has_value());
        expect(!result->find_interface("zebra").has_value());

        const auto gas = result->find_phase("gas").value();
        const auto solid = result->find_phase("solid").value();
        expect(!result->find_interface(gas, solid).has_value());
    };
}

} // namespace rift_test::phase_graph_lookup_missing_interface_00
