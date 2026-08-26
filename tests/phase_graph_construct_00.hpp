#pragma once

#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/phase_graph.hpp>

namespace rift_test::phase_graph_construct_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "a graph may contain one phase and no interfaces"_test = [] {
        const auto run = rift::test::make_test_run();
        const auto result = rift::make_phase_graph(run, {{"gas", "compressible"}}, {});

        expect(result.has_value());
        expect(result->phases().size() == 1_u);
        expect(result->interfaces().empty());
        expect(result->phases().front().id == rift::PhaseId::from_index(0));
        expect(result->phases().front().name == "gas");
        expect(result->phases().front().physics_key.value() == "compressible");
    };
}

} // namespace rift_test::phase_graph_construct_00
