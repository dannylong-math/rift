#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <utility>
#include <vector>

namespace rift_test::space_registry_groups_00 {

template<int dim> void check_independent_groups()
{
    using namespace boost::ut;

    auto fixture = rift::test::make_single_phase_space_fixture<dim>();

    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = fixture.gas, .name = "species", .components = 2, .polynomial_degree = 1},
                         {.phase = fixture.gas, .name = "flow", .components = dim + 2, .polynomial_degree = 2}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };
    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = fixture.gas, .mesh = fixture.mesh->id(), .locally_owned_requested_cells = fixture.cells}};
    rift::SpaceRegistry<dim> const registry(fixture.mesh);
    auto draft = registry.begin_draft(fixture.graph, std::move(specification), std::move(supports));

    expect(draft.has_value());
    auto snapshot = registry.finalize(draft.value(), {});
    expect(snapshot.has_value());
    expect(snapshot->field_spaces().size() == 2_u);

    const auto flow = rift::test::require_optional(snapshot->find_field(fixture.gas, "flow"));
    const auto species = rift::test::require_optional(snapshot->find_field(fixture.gas, "species"));
    expect(flow != species);
    expect(&snapshot->field_space(fixture.gas, flow).dof_handler() !=
           &snapshot->field_space(fixture.gas, species).dof_handler());
}

} // namespace rift_test::space_registry_groups_00

namespace rift_test::space_registry_groups_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "each phase field group owns an independent DoFHandler in 2D and 3D"_test = [] {
        check_independent_groups<2>();
        check_independent_groups<3>();
    };
}

} // namespace rift_test::space_registry_groups_00
