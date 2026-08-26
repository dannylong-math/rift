#pragma once

#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/phase_graph.hpp>

namespace rift_test::phase_graph_serialize_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "canonical serialization is independent of configuration insertion order"_test = [] {
        const auto run = rift::test::make_test_run();
        const auto first =
            rift::make_phase_graph(run, {{"liquid", "low-mach"}, {"gas", "compressible"}},
                                   {{"surface", "liquid", "gas", "finite-rate"}}, rift::test::accept_all_interfaces);
        const auto second =
            rift::make_phase_graph(run, {{"gas", "compressible"}, {"liquid", "low-mach"}},
                                   {{"surface", "liquid", "gas", "finite-rate"}}, rift::test::accept_all_interfaces);

        expect(first.has_value() && second.has_value());
        expect(first->canonical_json() == second->canonical_json());
        expect(
            first->canonical_json() ==
            R"({"schema":"rift.phase_graph","version":1,"phases":[{"id":0,"name":"gas","physics":"compressible"},{"id":1,"name":"liquid","physics":"low-mach"}],"interfaces":[{"id":0,"name":"surface","minus_phase":1,"plus_phase":0,"operator":"finite-rate"}]})");
    };
}

} // namespace rift_test::phase_graph_serialize_00
