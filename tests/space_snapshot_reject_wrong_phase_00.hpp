#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <rift/phase_graph.hpp>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rift_test::space_snapshot_reject_wrong_phase_00 {

template<int dim> void check_wrong_phase_rejected()
{
    using namespace boost::ut;

    auto triangulation = rift::test::make_two_cell_mesh<dim>();
    const auto ids = rift::test::active_cell_ids(*triangulation);
    auto run = rift::RunConfiguration::create(MPI_COMM_SELF).value();
    auto graph_result = rift::make_phase_graph(run, {{"gas", "compressible"}, {"liquid", "incompressible"}}, {});
    const auto graph = std::move(graph_result).value();
    const auto gas = graph.reference(rift::test::require_optional(graph.find_phase("gas"))).value();
    const auto liquid = graph.reference(rift::test::require_optional(graph.find_phase("liquid"))).value();
    const auto mesh = rift::make_mesh_snapshot(run, std::move(triangulation)).value();

    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = gas, .name = "flow", .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };
    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = gas,
         .mesh = mesh->id(),
         .locally_owned_requested_cells = rift::SupportEnvelope(ids.begin(), ids.end())}};
    rift::SpaceRegistry<dim> const registry(mesh);
    auto draft = registry.begin_draft(graph, std::move(specification), std::move(supports));
    auto snapshot = registry.finalize(draft.value(), {});
    const auto flow = rift::test::require_optional(snapshot->find_field(gas, "flow"));

    auto wrong_run = gas;
    wrong_run.graph.run = rift::RunConfigurationId::from_index(gas.graph.run.value() + 1);
    auto wrong_graph = gas;
    wrong_graph.graph.graph = rift::PhaseGraphInstanceId::from_index(gas.graph.graph.value() + 1);
    expect(!snapshot->find_field(wrong_run, "flow").has_value());
    expect(!snapshot->find_field(wrong_graph, "flow").has_value());
    expect(!snapshot->find_field(liquid, "flow").has_value());

    bool rejected = false;
    try {
        static_cast<void>(snapshot->field_space(liquid, flow));
    }
    catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected);
    for (const auto reference : {wrong_run, wrong_graph}) {
        rejected = false;
        try {
            static_cast<void>(snapshot->field_space(reference, flow));
        }
        catch (const std::invalid_argument&) {
            rejected = true;
        }
        expect(rejected);
    }
}

} // namespace rift_test::space_snapshot_reject_wrong_phase_00

namespace rift_test::space_snapshot_reject_wrong_phase_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "field identities cannot be resolved through the wrong phase in 2D and 3D"_test = [] {
        check_wrong_phase_rejected<2>();
        check_wrong_phase_rejected<3>();
    };
}

} // namespace rift_test::space_snapshot_reject_wrong_phase_00
