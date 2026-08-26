#pragma once

#include <algorithm>
#include <deal.II/base/point.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <memory>
#include <optional>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/run_configuration.hpp>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rift::test {

template<int dim> std::unique_ptr<dealii::Triangulation<dim>> make_two_cell_mesh()
{
    auto mesh = std::make_unique<dealii::Triangulation<dim>>();
    std::vector<unsigned int> repetitions(dim, 1);
    repetitions.front() = 2;
    dealii::Point<dim> upper;
    for (unsigned int direction = 0; std::cmp_less(direction, dim); ++direction) {
        upper(direction) = 1.0;
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

inline PhaseGraph make_single_phase_graph(const RunConfiguration& run)
{
    auto graph = make_phase_graph(run, {{"gas", "compressible"}}, {});
    return std::move(graph).value();
}

template<int dim> struct SinglePhaseSpaceFixture {
    /** Construct one complete same-run graph and mesh fixture. */
    SinglePhaseSpaceFixture(RunConfiguration run_value, PhaseGraph graph_value,
                            std::shared_ptr<const MeshSnapshot<dim>> mesh_value, const PhaseReference gas_value,
                            SupportEnvelope cells_value) :
        run(std::move(run_value)),
        graph(std::move(graph_value)),
        mesh(std::move(mesh_value)),
        gas(gas_value),
        cells(std::move(cells_value))
    {
    }

    RunConfiguration run;
    PhaseGraph graph;
    std::shared_ptr<const MeshSnapshot<dim>> mesh;
    PhaseReference gas;
    SupportEnvelope cells;
};

template<int dim> SinglePhaseSpaceFixture<dim> make_single_phase_space_fixture()
{
    auto run = RunConfiguration::create(MPI_COMM_SELF).value();
    auto graph = make_single_phase_graph(run);
    auto triangulation = make_two_cell_mesh<dim>();
    const auto ids = active_cell_ids(*triangulation);
    auto mesh = make_mesh_snapshot(run, std::move(triangulation)).value();
    const auto phase = graph.find_phase("gas");
    if (!phase) {
        throw std::logic_error("the single-phase graph did not contain gas");
    }
    const auto reference = graph.reference(*phase);
    if (!reference) {
        throw std::logic_error("the single-phase graph did not create a gas reference");
    }
    const auto gas = *reference;
    return {std::move(run), std::move(graph), std::move(mesh), gas, SupportEnvelope(ids.begin(), ids.end())};
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
    auto fixture = make_single_phase_space_fixture<dim>();

    SpaceSpecification specification{
        .phase_fields = {{.phase = fixture.gas, .name = "flow", .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };

    std::vector<PhaseSupportSpecification> supports{
        {.phase = fixture.gas, .mesh = fixture.mesh->id(), .locally_owned_requested_cells = fixture.cells}};
    SpaceRegistry<dim> const registry(fixture.mesh);
    auto draft = registry.begin_draft(fixture.graph, std::move(specification), std::move(supports));
    auto snapshot = registry.finalize(draft.value(), std::move(regional_entries));
    return std::move(snapshot).value();
}

template<int dim> StateStore make_state_store(const SpaceSnapshot<dim>& space)
{
    return rift::make_state_store(space.layout(), {}).value();
}

template<int dim> StateStore make_state_store(const SpaceSnapshot<dim>& space, const StateRetentionPolicy retention)
{
    return rift::make_state_store(space.layout(), retention).value();
}

template<int dim> StateFieldReference field_reference(const SpaceSnapshot<dim>& space, const FieldGroupId group)
{
    const auto reference = space.layout().field_reference(group);
    if (!reference) {
        throw std::logic_error("the requested test field was absent from the finalized layout");
    }
    return *reference;
}

} // namespace rift::test
