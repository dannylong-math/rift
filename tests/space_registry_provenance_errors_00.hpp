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

namespace rift_test::space_registry_provenance_errors_00 {

template<int dim> void check_provenance_errors()
{
    using namespace boost::ut;

    auto fixture = rift::test::make_single_phase_space_fixture<dim>();
    auto second_triangulation = rift::test::make_two_cell_mesh<dim>();
    const auto second_mesh = rift::make_mesh_snapshot(fixture.run, std::move(second_triangulation)).value();
    rift::SpaceRegistry<dim> const registry(fixture.mesh);
    const auto schema = [&] {
        return rift::SpaceSpecification{
            .phase_fields = {{.phase = fixture.gas, .name = "flow", .components = 1, .polynomial_degree = 1}},
            .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1}};
    };

    std::vector<rift::PhaseSupportSpecification> wrong_mesh{
        {.phase = fixture.gas, .mesh = second_mesh->id(), .locally_owned_requested_cells = fixture.cells}};
    const auto wrong_mesh_result = registry.begin_draft(fixture.graph, schema(), std::move(wrong_mesh));
    expect(rift::test::has_space_error(wrong_mesh_result.error(), rift::SpaceBuildErrorCode::support_mesh_mismatch));

    auto other_graph = rift::test::make_single_phase_graph(fixture.run);
    const auto foreign = other_graph.reference(rift::test::require_optional(other_graph.find_phase("gas"))).value();
    auto foreign_schema = schema();
    foreign_schema.phase_fields.front().phase = foreign;
    std::vector<rift::PhaseSupportSpecification> foreign_support{
        {.phase = foreign, .mesh = fixture.mesh->id(), .locally_owned_requested_cells = fixture.cells}};
    const auto foreign_result =
        registry.begin_draft(fixture.graph, std::move(foreign_schema), std::move(foreign_support));
    expect(rift::test::has_space_error(foreign_result.error(), rift::SpaceBuildErrorCode::phase_reference_mismatch));

    auto other_run = rift::RunConfiguration::create(MPI_COMM_SELF).value();
    auto other_run_graph = rift::test::make_single_phase_graph(other_run);
    const auto other_run_gas =
        other_run_graph.reference(rift::test::require_optional(other_run_graph.find_phase("gas"))).value();
    auto other_run_schema = schema();
    other_run_schema.phase_fields.front().phase = other_run_gas;
    std::vector<rift::PhaseSupportSpecification> other_run_support{
        {.phase = other_run_gas, .mesh = fixture.mesh->id(), .locally_owned_requested_cells = fixture.cells}};
    const auto other_run_result =
        registry.begin_draft(other_run_graph, std::move(other_run_schema), std::move(other_run_support));
    expect(rift::test::has_space_error(other_run_result.error(), rift::SpaceBuildErrorCode::graph_provenance_mismatch));

    auto foreign_run_reference_schema = schema();
    foreign_run_reference_schema.phase_fields.front().phase = other_run_gas;
    std::vector<rift::PhaseSupportSpecification> foreign_run_reference_support{
        {.phase = other_run_gas, .mesh = fixture.mesh->id(), .locally_owned_requested_cells = fixture.cells}};
    const auto foreign_run_reference_result = registry.begin_draft(
        fixture.graph, std::move(foreign_run_reference_schema), std::move(foreign_run_reference_support));
    expect(rift::test::has_space_error(foreign_run_reference_result.error(),
                                       rift::SpaceBuildErrorCode::phase_reference_mismatch));
}

} // namespace rift_test::space_registry_provenance_errors_00

namespace rift_test::space_registry_provenance_errors_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "space construction rejects wrong-mesh and cross-graph provenance"_test = [] {
        check_provenance_errors<2>();
        check_provenance_errors<3>();
    };
}

} // namespace rift_test::space_registry_provenance_errors_00
