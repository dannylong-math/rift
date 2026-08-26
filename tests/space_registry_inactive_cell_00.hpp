#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <utility>
#include <vector>

namespace rift_test::space_registry_inactive_cell_00 {

template<int dim> void check_refined_away_cell()
{
    using namespace boost::ut;

    auto run = rift::RunConfiguration::create(MPI_COMM_SELF).value();
    auto graph = rift::test::make_single_phase_graph(run);
    const auto gas = graph.reference(rift::test::require_optional(graph.find_phase("gas"))).value();
    auto triangulation = rift::test::make_two_cell_mesh<dim>();
    const auto stale = triangulation->begin_active()->id();
    triangulation->begin_active()->set_refine_flag();
    triangulation->execute_coarsening_and_refinement();
    auto mesh = rift::make_mesh_snapshot(run, std::move(triangulation)).value();
    rift::SpaceRegistry<dim> const registry(mesh);
    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = gas, .name = "flow", .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };
    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = gas, .mesh = mesh->id(), .locally_owned_requested_cells = {stale}}};
    const auto result = registry.begin_draft(graph, std::move(specification), std::move(supports));
    expect(!result.has_value());
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::inactive_support_cell));
}

} // namespace rift_test::space_registry_inactive_cell_00

namespace rift_test::space_registry_inactive_cell_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "refined-away support cell identities are inactive in 2D and 3D"_test = [] {
        check_refined_away_cell<2>();
        check_refined_away_cell<3>();
    };
}

} // namespace rift_test::space_registry_inactive_cell_00
