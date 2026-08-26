#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <utility>
#include <vector>

namespace rift_test::space_registry_disjoint_phases_00 {

template<int dim> void check_disjoint_phases()
{
    using namespace boost::ut;

    auto run = rift::RunConfiguration::create(MPI_COMM_SELF).value();
    auto graph = rift::make_phase_graph(run, {{"gas", "compressible"}, {"liquid", "incompressible"}}, {}).value();
    const auto gas = graph.reference(rift::test::require_optional(graph.find_phase("gas"))).value();
    const auto liquid = graph.reference(rift::test::require_optional(graph.find_phase("liquid"))).value();
    auto triangulation = rift::test::make_two_cell_mesh<dim>();
    const auto ids = rift::test::active_cell_ids(*triangulation);
    auto mesh = rift::make_mesh_snapshot(run, std::move(triangulation)).value();
    rift::SpaceRegistry<dim> const registry(mesh);
    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = liquid, .name = "flow", .components = dim + 1, .polynomial_degree = 1},
                         {.phase = gas, .name = "flow", .components = dim + 2, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };
    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = gas, .mesh = mesh->id(), .locally_owned_requested_cells = {ids.front()}},
        {.phase = liquid, .mesh = mesh->id(), .locally_owned_requested_cells = {ids.back()}}};
    const auto draft = registry.begin_draft(graph, std::move(specification), std::move(supports));
    expect(draft.has_value());
    for (const auto& field : draft->field_spaces()) {
        for (const auto& cell : field.dof_handler().active_cell_iterators()) {
            expect(cell->active_fe_index() ==
                   static_cast<unsigned int>(field.support().final_locally_owned_cells().contains(cell->id()) ? 0 : 1));
        }
    }
}

} // namespace rift_test::space_registry_disjoint_phases_00

namespace rift_test::space_registry_disjoint_phases_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "two phases retain disjoint masks for multicomponent fields"_test = [] {
        check_disjoint_phases<2>();
        check_disjoint_phases<3>();
    };
}

} // namespace rift_test::space_registry_disjoint_phases_00
