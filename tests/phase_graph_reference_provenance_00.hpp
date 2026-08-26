#pragma once

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>

namespace rift_test::phase_graph_reference_provenance_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "equal local phase IDs from separate graphs are not interchangeable"_test = [] {
        auto run = rift::RunConfiguration::create(MPI_COMM_SELF).value();
        auto first = rift::make_phase_graph(run, {{"gas", "compressible"}}, {}).value();
        auto second = rift::make_phase_graph(run, {{"gas", "compressible"}}, {}).value();
        const auto reference = first.reference(rift::PhaseId::from_index(0)).value();

        expect(!second.owns(reference));
        const auto lookup = second.phase(reference);
        expect(!lookup.has_value());
        expect(lookup.error().code == rift::PhaseGraphErrorCode::foreign_graph_reference);
    };

    "equal local phase IDs from separate runs are not interchangeable"_test = [] {
        auto first_run = rift::RunConfiguration::create(MPI_COMM_SELF).value();
        auto second_run = rift::RunConfiguration::create(MPI_COMM_SELF).value();
        auto first = rift::make_phase_graph(first_run, {{"gas", "compressible"}}, {}).value();
        auto second = rift::make_phase_graph(second_run, {{"gas", "compressible"}}, {}).value();
        const auto reference = first.reference(rift::PhaseId::from_index(0)).value();

        expect(!second.owns(reference));
        const auto lookup = second.phase(reference);
        expect(!lookup.has_value());
        expect(lookup.error().code == rift::PhaseGraphErrorCode::foreign_run_reference);
    };
}

} // namespace rift_test::phase_graph_reference_provenance_00
