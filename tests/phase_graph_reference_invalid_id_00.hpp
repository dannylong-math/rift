#pragma once

#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <limits>
#include <mpi.h>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>

namespace rift_test::phase_graph_reference_invalid_id_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "a graph rejects a phase ID outside its local descriptor array"_test = [] {
        auto run = rift::RunConfiguration::create(MPI_COMM_SELF).value();
        auto graph = rift::make_phase_graph(run, {{"gas", "compressible"}}, {}).value();

        const auto reference = graph.reference(rift::PhaseId::from_index(1));

        expect(!reference.has_value());
        expect(reference.error().code == rift::PhaseGraphErrorCode::invalid_phase_reference);

        const rift::PhaseReference forged{.graph = graph.provenance(), .phase = rift::PhaseId::from_index(1)};
        expect(!graph.owns(forged));
        const auto lookup = graph.phase(forged);
        expect(!lookup.has_value());
        expect(lookup.error().code == rift::PhaseGraphErrorCode::invalid_phase_reference);

        const auto maximum = graph.reference(rift::PhaseId::from_index(std::numeric_limits<std::uint32_t>::max()));
        expect(!maximum.has_value());
        expect(maximum.error().code == rift::PhaseGraphErrorCode::invalid_phase_reference);
    };
}

} // namespace rift_test::phase_graph_reference_invalid_id_00
