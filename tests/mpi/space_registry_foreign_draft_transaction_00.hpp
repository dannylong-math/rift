#pragma once

#include <algorithm>
#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <memory>
#include <mpi.h>
#include <optional>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rift_test::mpi::space_registry_foreign_draft_transaction_00 {

template<int dim> std::shared_ptr<const rift::MeshSnapshot<dim>> make_mesh(const rift::RunConfiguration& run)
{
    auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_WORLD);
    dealii::GridGenerator::subdivided_hyper_cube(*triangulation, 2);
    std::unique_ptr<dealii::Triangulation<dim>> consumed = std::move(triangulation);
    auto snapshot = rift::make_mesh_snapshot(run, std::move(consumed));
    if (!snapshot) {
        throw std::runtime_error("distributed mesh snapshot construction failed");
    }
    return std::move(snapshot).value();
}

template<int dim>
rift::SpaceDraftResult<dim>
make_draft(const rift::SpaceRegistry<dim>& registry, const rift::PhaseGraph& graph, const rift::PhaseReference phase,
           const std::shared_ptr<const rift::MeshSnapshot<dim>>& mesh, const std::string& name)
{
    rift::SupportEnvelope owned;
    for (const auto& cell : mesh->triangulation().active_cell_iterators()) {
        if (cell->is_locally_owned()) {
            owned.insert(cell->id());
        }
    }
    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = phase, .name = name, .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = name + "_level_sets", .components = 1, .polynomial_degree = 1},
    };
    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = phase, .mesh = mesh->id(), .locally_owned_requested_cells = std::move(owned)}};
    return registry.begin_draft(graph, std::move(specification), std::move(supports));
}

template<int dim> void check_foreign_transaction()
{
    using namespace boost::ut;
    const auto rank = dealii::Utilities::MPI::this_mpi_process(MPI_COMM_WORLD);
    auto run = rift::RunConfiguration::create(MPI_COMM_WORLD).value();
    auto graph = rift::make_phase_graph(run, {{"gas", "compressible"}}, {}).value();
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
    auto mesh_a = make_mesh<dim>(run);
    auto mesh_b = make_mesh<dim>(run);
    auto target_mesh = make_mesh<dim>(run);
    expect(mesh_a->id() != mesh_b->id());
    expect(target_mesh->id() != mesh_a->id());
    expect(target_mesh->id() != mesh_b->id());
    const std::weak_ptr<const rift::MeshSnapshot<dim>> weak_a = mesh_a;
    const std::weak_ptr<const rift::MeshSnapshot<dim>> weak_b = mesh_b;
    auto draft_a = [&] {
        rift::SpaceRegistry<dim> const registry(mesh_a);
        auto result = make_draft(registry, graph, gas, mesh_a, "a");
        expect(result.has_value());
        if (!result) {
            return std::optional<rift::SpaceDraft<dim>>{};
        }
        return std::optional<rift::SpaceDraft<dim>>(std::move(result).value());
    }();
    auto draft_b = [&] {
        rift::SpaceRegistry<dim> const registry(mesh_b);
        auto result = make_draft(registry, graph, gas, mesh_b, "b");
        expect(result.has_value());
        if (!result) {
            return std::optional<rift::SpaceDraft<dim>>{};
        }
        return std::optional<rift::SpaceDraft<dim>>(std::move(result).value());
    }();
    expect(draft_a.has_value());
    expect(draft_b.has_value());
    if (!draft_a || !draft_b) {
        return;
    }
    mesh_a.reset();
    mesh_b.reset();
    expect(!weak_a.expired());
    expect(!weak_b.expired());
    rift::SpaceRegistry<dim> const target_registry(target_mesh);

    rift::SpaceDraft<dim>& selected = rank == 0U ? *draft_b : *draft_a;
    const auto rejected = target_registry.finalize(selected, {});
    expect(!rejected.has_value());
    if (rejected) {
        return;
    }
    expect(std::ranges::any_of(rejected.error(), [](const auto& error) {
        return error.code == rift::SpaceBuildErrorCode::foreign_registry_draft;
    }));
    expect(draft_a->active());
    expect(draft_b->active());
    expect(!weak_a.expired());
    expect(!weak_b.expired());
    std::string diagnostic;
    for (const auto& error : rejected.error()) {
        diagnostic += std::to_string(static_cast<unsigned int>(error.code)) + ":" + error.message + ":" +
                      std::to_string(error.reporting_rank.value_or(-1)) + "\n";
    }
    const auto diagnostics = dealii::Utilities::MPI::all_gather(MPI_COMM_WORLD, diagnostic);
    expect(std::ranges::all_of(diagnostics, [&](const auto& candidate) { return candidate == diagnostic; }));

    draft_a.reset();
    expect(dealii::Utilities::MPI::sum(0U, MPI_COMM_WORLD) == 0_u);
    expect(weak_a.expired());
    expect(!weak_b.expired());
    draft_b.reset();
    expect(dealii::Utilities::MPI::sum(0U, MPI_COMM_WORLD) == 0_u);
    expect(weak_b.expired());
}

inline void register_tests()
{
    using namespace boost::ut;
    "foreign distributed drafts retain last mesh ownership until matched release in 2D and 3D"_test = [] {
        check_foreign_transaction<2>();
        check_foreign_transaction<3>();
    };
}

} // namespace rift_test::mpi::space_registry_foreign_draft_transaction_00
