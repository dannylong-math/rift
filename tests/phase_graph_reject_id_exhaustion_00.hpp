#pragma once

#include "../src/run_configuration_internal.hpp"
#include "phase_graph_test_support.hpp"

#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <limits>
#include <memory>
#include <mpi.h>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <utility>

namespace rift_test::phase_graph_reject_id_exhaustion_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "graph construction reports exhaustion of its finite instance-ID space"_test = [] {
        MPI_Comm owned = MPI_COMM_NULL;
        expect(MPI_Comm_dup(MPI_COMM_SELF, &owned) == MPI_SUCCESS);
        auto control = std::make_shared<rift::detail::RunConfigurationControl>(
            owned, rift::RunConfigurationId::from_index(0), std::numeric_limits<std::uint64_t>::max());
        auto run = rift::detail::RunConfigurationAccess::adopt(std::move(control));

        const auto graph = rift::make_phase_graph(run, {{"gas", "compressible"}}, {});

        expect(!graph.has_value());
        expect(rift::test::has_error(graph.error(), rift::PhaseGraphErrorCode::graph_id_allocation_failed));
    };
}

} // namespace rift_test::phase_graph_reject_id_exhaustion_00
