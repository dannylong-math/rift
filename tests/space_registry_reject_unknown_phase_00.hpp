#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <rift/phase_graph.hpp>
#include <utility>
#include <vector>

namespace rift_test::space_registry_reject_unknown_phase_00 {

template<int dim> void check_unknown_phase_rejected()
{
    using namespace boost::ut;

    auto fixture = rift::test::make_single_phase_space_fixture<dim>();
    const rift::PhaseReference unknown{.graph = fixture.graph.provenance(), .phase = rift::PhaseId::from_index(12)};
    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = unknown, .name = "flow", .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };

    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = unknown, .mesh = fixture.mesh->id(), .locally_owned_requested_cells = {*fixture.cells.begin()}}};
    rift::SpaceRegistry<dim> const registry(fixture.mesh);
    const auto result = registry.begin_draft(fixture.graph, std::move(specification), std::move(supports));

    expect(!result.has_value());
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::unknown_phase));
}

} // namespace rift_test::space_registry_reject_unknown_phase_00

namespace rift_test::space_registry_reject_unknown_phase_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "field groups must refer to a phase in the graph in 2D and 3D"_test = [] {
        check_unknown_phase_rejected<2>();
        check_unknown_phase_rejected<3>();
    };
}

} // namespace rift_test::space_registry_reject_unknown_phase_00
