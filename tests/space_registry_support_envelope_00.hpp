#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <utility>
#include <vector>

namespace rift_test::space_registry_support_envelope_00 {

template<int dim> void check_support_envelope()
{
    using namespace boost::ut;

    auto fixture = rift::test::make_single_phase_space_fixture<dim>();
    const auto requested = *fixture.cells.begin();

    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = fixture.gas, .name = "flow", .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };
    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = fixture.gas, .mesh = fixture.mesh->id(), .locally_owned_requested_cells = {requested}}};
    rift::SpaceRegistry<dim> const registry(fixture.mesh);
    auto draft = registry.begin_draft(fixture.graph, std::move(specification), std::move(supports));
    auto snapshot = registry.finalize(draft.value(), {});
    const auto& space =
        snapshot->field_space(fixture.gas, rift::test::require_optional(snapshot->find_field(fixture.gas, "flow")));

    for (const auto& cell : space.dof_handler().active_cell_iterators()) {
        if (cell->id() == requested) {
            expect(cell->active_fe_index() == 0_u);
            expect(cell->get_fe().dofs_per_cell > 0_u);
        }
        else {
            expect(cell->active_fe_index() == 1_u);
            expect(cell->get_fe().dofs_per_cell == 0_u);
        }
    }
}

} // namespace rift_test::space_registry_support_envelope_00

namespace rift_test::space_registry_support_envelope_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "phase cells outside the support envelope own no DoFs in 2D and 3D"_test = [] {
        check_support_envelope<2>();
        check_support_envelope<3>();
    };
}

} // namespace rift_test::space_registry_support_envelope_00
