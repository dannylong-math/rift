#pragma once

#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/phase_graph.hpp>

namespace rift_test::phase_graph_serialize_multiple_interfaces_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "canonical serialization separates multiple interfaces"_test = [] {
        const auto run = rift::test::make_test_run();
        const auto result = rift::make_phase_graph(
            run, {{"gas", "compressible"}, {"liquid", "low-mach"}, {"solid", "elastic"}},
            {{"evaporation", "liquid", "gas", "finite-rate"}, {"melting", "solid", "liquid", "stefan"}},
            rift::test::accept_all_interfaces);

        expect(result.has_value());
        expect(result->canonical_json().contains(R"("operator":"finite-rate"},{"id":1,"name":"melting")"));
    };
}

} // namespace rift_test::phase_graph_serialize_multiple_interfaces_00
