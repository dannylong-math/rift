#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <utility>
#include <vector>

namespace rift_test::space_registry_level_set_background_00 {

template<int dim> void check_full_background_level_set()
{
    using namespace boost::ut;

    auto fixture = rift::test::make_single_phase_space_fixture<dim>();

    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = fixture.gas, .name = "flow", .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 2, .polynomial_degree = 1},
    };
    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = fixture.gas, .mesh = fixture.mesh->id(), .locally_owned_requested_cells = {*fixture.cells.begin()}}};
    rift::SpaceRegistry<dim> const registry(fixture.mesh);
    auto draft = registry.begin_draft(fixture.graph, std::move(specification), std::move(supports));
    expect(draft.has_value());
    expect(draft->level_set_space().components() == 2_u);
    auto snapshot = registry.finalize(draft.value(), {});

    for (const auto& cell : snapshot->level_set_space().dof_handler().active_cell_iterators()) {
        expect(cell->active_fe_index() == 0_u);
        expect(cell->get_fe().dofs_per_cell > 0_u);
    }
}

} // namespace rift_test::space_registry_level_set_background_00

namespace rift_test::space_registry_level_set_background_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "the level-set field group covers the complete 2D and 3D background mesh"_test = [] {
        check_full_background_level_set<2>();
        check_full_background_level_set<3>();
    };
}

} // namespace rift_test::space_registry_level_set_background_00
