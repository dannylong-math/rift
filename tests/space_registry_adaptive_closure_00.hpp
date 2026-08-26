#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <cmath>
#include <cstddef>
#include <deal.II/base/mpi.h>
#include <deal.II/base/types.h>
#include <deal.II/fe/mapping_q1.h>
#include <deal.II/grid/tria.h>
#include <deal.II/lac/vector.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <set>
#include <utility>
#include <vector>

namespace rift_test::space_registry_adaptive_closure_00 {

template<int dim> rift::SupportEnvelope requested_support(dealii::Triangulation<dim>& triangulation)
{
    rift::SupportEnvelope requested;
    for (const auto& cell : triangulation.active_cell_iterators()) {
        if (cell->center()(0) < 0.5) {
            requested.insert(cell->id());
            break;
        }
    }
    for (const auto& cell : triangulation.active_cell_iterators()) {
        if (cell->center()(0) > 0.5 && std::abs(cell->face(0)->center()(0) - 0.5) < 1.0e-14) {
            requested.insert(cell->id());
            break;
        }
    }
    return requested;
}

template<int dim, typename SupportPoints>
bool has_hanging_interface_constraint(const rift::FieldGroupSpace<dim>& field, const SupportPoints& support_points)
{
    for (const auto& line : field.constraints().get_lines()) {
        const auto& point = support_points.at(line.index);
        bool is_hanging_point = false;
        for (unsigned int direction = 1; std::cmp_less(direction, dim); ++direction) {
            is_hanging_point = is_hanging_point || (point(direction) > 0.0 && point(direction) < 1.0);
        }
        if (std::abs(point(0) - 0.5) < 1.0e-14 && is_hanging_point && !line.entries.empty()) {
            return true;
        }
    }
    return false;
}

template<int dim, typename SupportPoints>
void verify_constraint_reproduction(const rift::FieldGroupSpace<dim>& field, const SupportPoints& support_points)
{
    using namespace boost::ut;
    for (int component = -1; component < dim; ++component) {
        dealii::Vector<double> values(field.dof_handler().n_dofs());
        for (const auto& [dof, point] : support_points) {
            values(dof) = component < 0 ? 1.0 : point(static_cast<unsigned int>(component));
        }
        const auto expected = values;
        field.constraints().distribute(values);
        for (dealii::types::global_dof_index dof = 0; dof < values.size(); ++dof) {
            expect(std::abs(values(dof) - expected(dof)) < 1.0e-13);
        }
    }
}

template<int dim> void check_adaptive_closure()
{
    using namespace boost::ut;

    auto run = rift::RunConfiguration::create(MPI_COMM_SELF).value();
    auto graph = rift::test::make_single_phase_graph(run);
    const auto gas_id = graph.find_phase("gas");
    expect(gas_id.has_value());
    if (!gas_id) {
        return;
    }
    const auto gas_result = graph.reference(*gas_id);
    expect(gas_result.has_value());
    if (!gas_result) {
        return;
    }
    const auto gas = *gas_result;
    auto triangulation = rift::test::make_two_cell_mesh<dim>();
    for (const auto& cell : triangulation->active_cell_iterators()) {
        if (cell->center()(0) > 0.5) {
            cell->set_refine_flag();
        }
    }
    triangulation->execute_coarsening_and_refinement();

    auto requested = requested_support(*triangulation);
    expect(requested.size() == 2_u);

    auto mesh = rift::make_mesh_snapshot(run, std::move(triangulation)).value();
    rift::SpaceRegistry<dim> const registry(mesh);
    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = gas, .name = "flow", .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };
    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = gas, .mesh = mesh->id(), .locally_owned_requested_cells = requested}};
    auto draft = registry.begin_draft(graph, std::move(specification), std::move(supports));
    expect(draft.has_value());

    const auto& support = draft->field_spaces().front().support();
    expect(support.requested_locally_owned_cells().size() == 2_u);
    constexpr std::size_t expected_added = dim == 2 ? 1 : 3;
    constexpr std::size_t expected_final = dim == 2 ? 3 : 5;
    constexpr dealii::types::global_dof_index expected_field_dofs = dim == 2 ? 8 : 22;
    constexpr dealii::types::global_dof_index expected_level_set_dofs = dim == 2 ? 11 : 31;
    expect(support.closure_added_locally_owned_cells().size() == expected_added);
    expect(support.final_locally_owned_cells().size() == expected_final);
    expect(draft->field_spaces().front().dof_handler().n_dofs() == expected_field_dofs);
    expect(draft->level_set_space().dof_handler().n_dofs() == expected_level_set_dofs);

    const auto& field = draft->field_spaces().front();
    const auto support_points =
        dealii::DoFTools::map_dofs_to_support_points(dealii::MappingQ1<dim>(), field.dof_handler());
    expect(field.constraints().n_constraints() > 0_u);
    expect(has_hanging_interface_constraint(field, support_points));
    verify_constraint_reproduction(field, support_points);

    rift::SpaceSpecification empty_specification{
        .phase_fields = {{.phase = gas, .name = "empty", .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "empty_level_sets", .components = 1, .polynomial_degree = 1},
    };
    std::vector<rift::PhaseSupportSpecification> empty_supports{
        {.phase = gas, .mesh = mesh->id(), .locally_owned_requested_cells = {}}};
    const auto empty_draft = registry.begin_draft(graph, std::move(empty_specification), std::move(empty_supports));
    expect(empty_draft.has_value());
    expect(empty_draft->field_spaces().front().support().final_locally_owned_cells().empty());
}

} // namespace rift_test::space_registry_adaptive_closure_00

namespace rift_test::space_registry_adaptive_closure_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "hanging-face support reaches the exact 2D and 3D least fixed point"_test = [] {
        check_adaptive_closure<2>();
        check_adaptive_closure<3>();
    };
}

} // namespace rift_test::space_registry_adaptive_closure_00
