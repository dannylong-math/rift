#pragma once

#include <algorithm>
#include <deal.II/base/point.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <memory>
#include <optional>
#include <rift/discrete_state.hpp>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rift::test {

template<int dim> std::shared_ptr<dealii::Triangulation<dim>> make_two_cell_mesh()
{
    auto mesh = std::make_shared<dealii::Triangulation<dim>>();
    std::vector<unsigned int> repetitions(dim, 1);
    repetitions.front() = 2;
    dealii::Point<dim> upper;
    for (unsigned int direction = 0; std::cmp_less(direction, dim); ++direction) {
        // The loop condition proves that this coordinate lies within the fixed-size point.
        upper[direction] = 1.0; // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    }
    dealii::GridGenerator::subdivided_hyper_rectangle(*mesh, repetitions, dealii::Point<dim>(), upper);
    return mesh;
}

template<int dim> std::vector<dealii::CellId> active_cell_ids(const dealii::Triangulation<dim>& mesh)
{
    std::vector<dealii::CellId> ids;
    for (const auto& cell : mesh.active_cell_iterators()) {
        ids.push_back(cell->id());
    }
    return ids;
}

inline PhaseGraph make_single_phase_graph()
{
    auto graph = make_phase_graph({{"gas", "compressible"}}, {});
    return std::move(graph).value();
}

inline bool has_space_error(const SpaceBuildErrors& errors, const SpaceBuildErrorCode code)
{
    return std::ranges::any_of(errors, [code](const auto& error) { return error.code == code; });
}

template<class Value> Value require_optional(const std::optional<Value> value)
{
    if (!value.has_value()) {
        throw std::logic_error("a required test-fixture lookup did not resolve");
    }
    return *value;
}

template<int dim>
SpaceSnapshot<dim> make_space_with_one_phase_field(std::vector<RegionalEntrySpecification> regional_entries = {})
{
    auto mesh = make_two_cell_mesh<dim>();
    const auto ids = active_cell_ids(*mesh);
    const auto graph = make_single_phase_graph();
    const auto gas = require_optional(graph.find_phase("gas"));

    SpaceSpecification specification{
        .phase_fields = {{.phase = gas,
                          .name = "flow",
                          .components = 1,
                          .polynomial_degree = 1,
                          .support_envelope = SupportEnvelope(ids.begin(), ids.end())}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };

    SpaceRegistry<dim> const registry(mesh, MPI_COMM_SELF);
    auto draft = registry.begin_draft(graph, std::move(specification));
    auto snapshot = registry.finalize(std::move(draft).value(), std::move(regional_entries));
    return std::move(snapshot).value();
}

} // namespace rift::test
