#pragma once

#include "../src/run_configuration_internal.hpp"

#include <algorithm>
#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <deal.II/grid/cell_id.h>
#include <exception>
#include <memory>
#include <mpi.h>
#include <mpi_proto.h>
#include <rift/discrete_state.hpp>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>

namespace rift_test::space_registry_diagnostic_merge_00 {

constexpr rift::detail::MpiAbort throw_fatal = []<class... Arguments>(Arguments... arguments) -> int {
    static_cast<void>(sizeof...(arguments));
    std::terminate();
};

} // namespace rift_test::space_registry_diagnostic_merge_00

namespace rift_test::space_registry_diagnostic_merge_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "collective diagnostics preserve structured keys and exactly deduplicate"_test = [] {
        MPI_Comm owned = MPI_COMM_NULL;
        expect(MPI_Comm_dup(MPI_COMM_SELF, &owned) == MPI_SUCCESS);
        const auto control = std::make_shared<const rift::detail::RunConfigurationControl>(
            owned, rift::RunConfigurationId::from_index(9), 0, throw_fatal);
        const rift::PhaseReference phase{.graph = {.run = rift::RunConfigurationId::from_index(9),
                                                   .graph = rift::PhaseGraphInstanceId::from_index(4)},
                                         .phase = rift::PhaseId::from_index(2)};
        const dealii::CellId first("0_0:");
        const dealii::CellId second("1_0:");
        rift::SpaceBuildErrors local;
        rift::detail::add_space_error(local, rift::SpaceBuildErrorCode::unknown_support_cell, "second", phase, second);
        rift::detail::add_space_error(local, rift::SpaceBuildErrorCode::nonowned_support_cell, "nonowned", phase,
                                      second);
        rift::detail::add_space_error(local, rift::SpaceBuildErrorCode::unknown_support_cell, "first", phase, first);
        rift::detail::add_space_error(local, rift::SpaceBuildErrorCode::unknown_support_cell, "first", phase, first);

        const auto merged = rift::detail::collective_space_errors(control, MPI_COMM_SELF, local);
        expect(merged.size() == 3_u);
        expect(merged.at(0).message == "nonowned");
        expect(merged.at(1).message == "first");
        expect(merged.at(1).phase.has_value());
        expect(merged.at(1).phase->graph.run == phase.graph.run);
        expect(merged.at(1).phase->graph.graph == phase.graph.graph);
        expect(merged.at(1).phase->phase == phase.phase);
        expect(merged.at(1).cell == first);
        expect(merged.at(1).reporting_rank == 0);
        expect(merged.at(2).message == "second");
        expect(merged.at(2).cell == second);

        const rift::PhaseReference zero_phase{.graph = {.run = rift::RunConfigurationId::from_index(0),
                                                        .graph = rift::PhaseGraphInstanceId::from_index(0)},
                                              .phase = rift::PhaseId::from_index(0)};
        rift::SpaceBuildErrors collision;
        rift::detail::add_space_error(collision, rift::SpaceBuildErrorCode::unknown_support_cell, "collision");
        rift::detail::add_space_error(collision, rift::SpaceBuildErrorCode::unknown_support_cell, "collision",
                                      zero_phase);
        const auto distinct = rift::detail::collective_space_errors(control, MPI_COMM_SELF, collision);
        expect(distinct.size() == 2_u);
        expect(std::ranges::count_if(distinct, [](const auto& error) { return error.phase.has_value(); }) == 1);
    };
}

} // namespace rift_test::space_registry_diagnostic_merge_00
