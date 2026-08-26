#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <utility>
#include <vector>

namespace rift_test::space_registry_uniform_degree_00 {

template<int dim> void check_uniform_degree()
{
    using namespace boost::ut;

    auto fixture = rift::test::make_single_phase_space_fixture<dim>();

    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = fixture.gas, .name = "flow", .components = 1, .polynomial_degree = 2}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 3},
    };
    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = fixture.gas, .mesh = fixture.mesh->id(), .locally_owned_requested_cells = fixture.cells}};
    rift::SpaceRegistry<dim> const registry(fixture.mesh);
    auto draft = registry.begin_draft(fixture.graph, std::move(specification), std::move(supports));
    auto snapshot = registry.finalize(draft.value(), {});
    const auto& field =
        snapshot->field_space(fixture.gas, rift::test::require_optional(snapshot->find_field(fixture.gas, "flow")));

    for (const auto& cell : field.dof_handler().active_cell_iterators()) {
        expect(cell->get_fe().degree == 2_u);
    }
    for (const auto& cell : snapshot->level_set_space().dof_handler().active_cell_iterators()) {
        expect(cell->get_fe().degree == 3_u);
    }
}

} // namespace rift_test::space_registry_uniform_degree_00

namespace rift_test::space_registry_uniform_degree_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "every active cell uses its field group's uniform degree in 2D and 3D"_test = [] {
        check_uniform_degree<2>();
        check_uniform_degree<3>();
    };
}

} // namespace rift_test::space_registry_uniform_degree_00
