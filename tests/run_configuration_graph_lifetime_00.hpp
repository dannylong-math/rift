#pragma once

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <optional>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <utility>

namespace rift_test::run_configuration_graph_lifetime_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "a graph retains its run after the source communicator and handle are released"_test = [] {
        MPI_Comm source = MPI_COMM_NULL;
        expect(MPI_Comm_dup(MPI_COMM_SELF, &source) == MPI_SUCCESS);

        std::optional<rift::PhaseGraph> graph;
        {
            auto run = rift::RunConfiguration::create(source).value();
            int relationship = MPI_UNEQUAL;
            expect(MPI_Comm_compare(source, run.communicator(), &relationship) == MPI_SUCCESS);
            expect(relationship == MPI_CONGRUENT);
            auto moved_run = std::move(run);
            expect(MPI_Comm_free(&source) == MPI_SUCCESS);
            graph.emplace(rift::make_phase_graph(moved_run, {{"gas", "compressible"}}, {}).value());
        }

        const auto gas = graph->reference(rift::PhaseId::from_index(0)).value();
        expect(graph->owns(gas));
        expect(graph->phase(gas)->get().name == "gas");
    };

    "a graph copy owns independent descriptor storage and shared run provenance"_test = [] {
        auto run = rift::RunConfiguration::create(MPI_COMM_SELF).value();
        std::optional<rift::PhaseGraph> copy;
        std::optional<rift::PhaseGraph> moved;
        std::optional<rift::PhaseReference> reference;
        {
            auto original = rift::make_phase_graph(run, {{"gas", "compressible"}}, {}).value();
            copy.emplace(original);
            reference = original.reference(rift::PhaseId::from_index(0)).value();
            expect(&copy->phase(rift::PhaseId::from_index(0)) != &original.phase(rift::PhaseId::from_index(0)));
            expect(copy->owns(*reference));
            moved.emplace(std::move(*copy));
        }

        expect(moved->phase(rift::PhaseId::from_index(0)).name == "gas");
        expect(moved->provenance().run == run.id());
        expect(moved->owns(*reference));
        expect(moved->phase(*reference)->get().name == "gas");
    };
}

} // namespace rift_test::run_configuration_graph_lifetime_00
