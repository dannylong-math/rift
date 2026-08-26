#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <deal.II/fe/fe_data.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <utility>
#include <vector>

namespace rift_test::space_registry_non_dominating_nothing_00 {

template<int dim> void check_non_dominating_nothing()
{
    using namespace boost::ut;

    auto fixture = rift::test::make_single_phase_space_fixture<dim>();

    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = fixture.gas, .name = "flow", .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };
    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = fixture.gas, .mesh = fixture.mesh->id(), .locally_owned_requested_cells = {*fixture.cells.begin()}}};
    rift::SpaceRegistry<dim> const registry(fixture.mesh);
    auto draft = registry.begin_draft(fixture.graph, std::move(specification), std::move(supports));
    auto snapshot = registry.finalize(draft.value(), {});
    const auto& space =
        snapshot->field_space(fixture.gas, rift::test::require_optional(snapshot->find_field(fixture.gas, "flow")));

    expect(space.dof_handler().get_fe(1).compare_for_domination(space.dof_handler().get_fe(0), 1) ==
           dealii::FiniteElementDomination::no_requirements);
    expect(space.constraints().n_constraints() == 0_u);
}

} // namespace rift_test::space_registry_non_dominating_nothing_00

namespace rift_test::space_registry_non_dominating_nothing_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "FE_Nothing does not constrain the active phase trace in 2D and 3D"_test = [] {
        check_non_dominating_nothing<2>();
        check_non_dominating_nothing<3>();
    };
}

} // namespace rift_test::space_registry_non_dominating_nothing_00
