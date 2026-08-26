#pragma once

#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/phase_graph.hpp>

namespace rift_test::phase_graph_errors_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "construction reports independent configuration errors together"_test = [] {
        const auto run = rift::test::make_test_run();
        const auto result =
            rift::make_phase_graph(run, {{"gas", "compressible"}, {"gas", "low-mach"}, {"liquid", "low-mach"}},
                                   {{"missing", "unknown", "liquid", "law"}, {"self", "liquid", "liquid", "law"}},
                                   rift::test::accept_all_interfaces);

        expect(!result.has_value());
        expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::duplicate_phase_name));
        expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::missing_incident_phase));
        expect(rift::test::has_error(result.error(), rift::PhaseGraphErrorCode::self_interface));
    };
}

} // namespace rift_test::phase_graph_errors_00
