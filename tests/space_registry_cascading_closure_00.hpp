#pragma once

#include "discrete_state_test_support.hpp"

#include <algorithm>
#include <boost/ut.hpp>
#include <deal.II/base/geometry_info.h>
#include <deal.II/base/mpi.h>
#include <deal.II/base/point.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <memory>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <set>
#include <utility>
#include <vector>

namespace rift_test::space_registry_cascading_closure_00 {

template<int dim> std::unique_ptr<dealii::Triangulation<dim>> make_cascading_mesh()
{
    auto mesh = std::make_unique<dealii::Triangulation<dim>>();
    std::vector<unsigned int> repetitions(dim, 1);
    repetitions.at(0) = 2;
    repetitions.at(1) = 2;
    dealii::Point<dim> upper;
    for (unsigned int direction = 0; std::cmp_less(direction, dim); ++direction) {
        upper(direction) = 1.0;
    }
    dealii::GridGenerator::subdivided_hyper_rectangle(*mesh, repetitions, dealii::Point<dim>(), upper);
    for (const auto& cell : mesh->active_cell_iterators()) {
        if (cell->center()(0) > 0.5) {
            cell->set_refine_flag();
        }
    }
    mesh->execute_coarsening_and_refinement();
    for (const auto& cell : mesh->active_cell_iterators()) {
        if (cell->center()(0) > 0.5 && cell->center()(1) > 0.5 && cell->center()(1) < 0.75) {
            cell->set_refine_flag();
        }
    }
    mesh->execute_coarsening_and_refinement();
    return mesh;
}

template<int dim>
std::pair<rift::SupportEnvelope, unsigned int> independent_closure(const dealii::Triangulation<dim>& mesh,
                                                                   rift::SupportEnvelope mask)
{
    unsigned int changed_iterations = 0;
    while (true) {
        rift::SupportEnvelope additions;
        for (const auto& fine : mesh.active_cell_iterators()) {
            for (unsigned int face = 0; face < dealii::GeometryInfo<dim>::faces_per_cell; ++face) {
                if (fine->at_boundary(face) || !fine->neighbor_is_coarser(face)) {
                    continue;
                }
                const auto [coarse_face, subface] = fine->neighbor_of_coarser_neighbor(face);
                static_cast<void>(subface);
                const auto coarse = fine->neighbor(face);
                rift::SupportEnvelope interface{coarse->id()};
                for (unsigned int child = 0; child < dealii::GeometryInfo<dim>::max_children_per_face; ++child) {
                    interface.insert(coarse->neighbor_child_on_subface(coarse_face, child)->id());
                }
                if (std::ranges::any_of(interface, [&](const auto& id) { return mask.contains(id); })) {
                    additions.insert(interface.begin(), interface.end());
                }
            }
        }
        const auto old_size = mask.size();
        mask.insert(additions.begin(), additions.end());
        if (mask.size() == old_size) {
            return {std::move(mask), changed_iterations};
        }
        ++changed_iterations;
    }
}

template<int dim> void check_cascading_closure()
{
    using namespace boost::ut;
    auto run = rift::RunConfiguration::create(MPI_COMM_SELF).value();
    auto graph = rift::test::make_single_phase_graph(run);
    const auto gas = graph.reference(rift::test::require_optional(graph.find_phase("gas"))).value();
    auto triangulation = make_cascading_mesh<dim>();
    rift::SupportEnvelope requested;
    rift::SupportEnvelope expected;
    unsigned int changed_iterations = 0;
    for (const auto& cell : triangulation->active_cell_iterators()) {
        rift::SupportEnvelope candidate{cell->id()};
        auto candidate_closure = independent_closure(*triangulation, candidate);
        if (candidate_closure.second > 1) {
            requested = std::move(candidate);
            expected = std::move(candidate_closure.first);
            changed_iterations = candidate_closure.second;
            break;
        }
    }
    expect(requested.size() == 1_u);
    expect(changed_iterations > 1_u);

    auto mesh = rift::make_mesh_snapshot(run, std::move(triangulation)).value();
    rift::SpaceRegistry<dim> const registry(mesh);
    const auto specification = [&] {
        return rift::SpaceSpecification{
            .phase_fields = {{.phase = gas, .name = "flow", .components = 1, .polynomial_degree = 1}},
            .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
        };
    };
    const auto supports = [&] {
        return std::vector<rift::PhaseSupportSpecification>{
            {.phase = gas, .mesh = mesh->id(), .locally_owned_requested_cells = requested}};
    };
    const auto limited = rift::detail::SpaceRegistryAccess<dim>::begin_draft(
        registry, graph, specification(), supports(), rift::SpaceEpoch::from_index(71), 1);
    expect(!limited.has_value());
    expect(rift::test::has_space_error(limited.error(), rift::SpaceBuildErrorCode::closure_nonconvergence));

    const auto draft = registry.begin_draft(graph, specification(), supports());
    expect(draft.has_value());
    if (!draft.has_value()) {
        return;
    }
    expect(draft.value().field_spaces().front().support().final_locally_owned_cells() == expected);
}

} // namespace rift_test::space_registry_cascading_closure_00

namespace rift_test::space_registry_cascading_closure_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "multilevel hanging support needs multiple global growth iterations in 2D and 3D"_test = [] {
        check_cascading_closure<2>();
        check_cascading_closure<3>();
    };
}

} // namespace rift_test::space_registry_cascading_closure_00
