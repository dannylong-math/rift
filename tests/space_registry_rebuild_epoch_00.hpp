#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <utility>
#include <vector>

namespace rift_test::space_registry_rebuild_epoch_00 {

template<int dim> void check_rebuild_epoch()
{
    using namespace boost::ut;

    auto fixture = rift::test::make_single_phase_space_fixture<dim>();
    rift::SpaceRegistry<dim> const registry(fixture.mesh);

    rift::SpaceSpecification first_specification{
        .phase_fields = {{.phase = fixture.gas, .name = "flow", .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };
    std::vector<rift::PhaseSupportSpecification> first_support{
        {.phase = fixture.gas, .mesh = fixture.mesh->id(), .locally_owned_requested_cells = {*fixture.cells.begin()}}};
    auto first_draft = registry.begin_draft(fixture.graph, std::move(first_specification), std::move(first_support));
    auto first = registry.finalize(first_draft.value(), {});

    rift::SpaceSpecification second_specification{
        .phase_fields = {{.phase = fixture.gas, .name = "flow", .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };
    std::vector<rift::PhaseSupportSpecification> second_support{
        {.phase = fixture.gas, .mesh = fixture.mesh->id(), .locally_owned_requested_cells = fixture.cells}};
    auto second_draft = registry.begin_draft(fixture.graph, std::move(second_specification), std::move(second_support));
    auto second = registry.finalize(second_draft.value(), {});

    expect(first->epoch() != second->epoch());
    expect(first->field_spaces().front().dof_handler().n_dofs() <
           second->field_spaces().front().dof_handler().n_dofs());
}

} // namespace rift_test::space_registry_rebuild_epoch_00

namespace rift_test::space_registry_rebuild_epoch_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "support-envelope rebuilds receive a new epoch in 2D and 3D"_test = [] {
        check_rebuild_epoch<2>();
        check_rebuild_epoch<3>();
    };
}

} // namespace rift_test::space_registry_rebuild_epoch_00
