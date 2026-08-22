#pragma once

#include <deal.II/base/point.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <memory>
#include <rift/discrete_state.hpp>
#include <set>
#include <utility>
#include <vector>

namespace rift::test {

template<int dim> std::shared_ptr<dealii::Triangulation<dim>> make_two_cell_mesh()
{
    auto mesh = std::make_shared<dealii::Triangulation<dim>>();
    std::vector<unsigned int> repetitions(dim, 1);
    repetitions.front() = 2;
    dealii::Point<dim> upper;
    for (unsigned int direction = 0; direction < dim; ++direction)
        upper[direction] = 1.0;
    dealii::GridGenerator::subdivided_hyper_rectangle(*mesh, repetitions, dealii::Point<dim>(), upper);
    return mesh;
}

template<int dim> std::vector<dealii::CellId> active_cell_ids(const dealii::Triangulation<dim>& mesh)
{
    std::vector<dealii::CellId> ids;
    for (const auto& cell : mesh.active_cell_iterators())
        ids.push_back(cell->id());
    return ids;
}

inline PhaseGraph make_single_phase_graph()
{
    auto graph = make_phase_graph({{"gas", "compressible"}}, {});
    return std::move(graph).value();
}

inline bool has_space_error(const SpaceBuildErrors& errors, const SpaceBuildErrorCode code)
{
    for (const auto& error : errors)
        if (error.code == code)
            return true;
    return false;
}

template<int dim>
SpaceSnapshot<dim> make_space_with_one_phase_field(std::vector<RegionalEntrySpecification> regional_entries = {})
{
    auto mesh = make_two_cell_mesh<dim>();
    const auto ids = active_cell_ids(*mesh);
    const auto graph = make_single_phase_graph();
    const auto gas = graph.find_phase("gas").value();

    SpaceSpecification specification{
        .phase_fields = {{gas, "flow", 1, 1, SupportEnvelope(ids.begin(), ids.end())}},
        .level_set = {"level_sets", 1, 1},
    };

    SpaceRegistry<dim> registry(mesh, MPI_COMM_SELF);
    auto draft = registry.begin_draft(graph, std::move(specification));
    auto snapshot = registry.finalize(std::move(draft).value(), std::move(regional_entries));
    return std::move(snapshot).value();
}

} // namespace rift::test
