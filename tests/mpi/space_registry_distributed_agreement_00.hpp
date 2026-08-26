#pragma once

#include <algorithm>
#include <array>
#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <deal.II/base/types.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <memory>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace rift_test::mpi::space_registry_distributed_agreement_00 {

template<typename T> T rank_one_value(const unsigned int rank, T divergent, T common)
{
    return rank == 1U ? std::move(divergent) : std::move(common);
}

template<typename T> T rank_zero_value(const unsigned int rank, T root, T other)
{
    return rank == 0U ? std::move(root) : std::move(other);
}

template<typename Result> bool has_error_code(const Result& result, const rift::SpaceBuildErrorCode code)
{
    return !result && std::ranges::any_of(result.error(), [code](const auto& error) { return error.code == code; });
}

inline std::vector<rift::PhaseFieldGroupSpecification>
rank_permuted_fields(std::vector<rift::PhaseFieldGroupSpecification> fields, const unsigned int rank)
{
    if (rank == 1U) {
        std::ranges::reverse(fields);
    }
    return fields;
}

template<int dim>
void expect_schema_mismatch(const rift::SpaceRegistry<dim>& registry, const rift::PhaseGraph& graph,
                            const std::vector<rift::PhaseSupportSpecification>& supports,
                            rift::SpaceSpecification divergent)
{
    using namespace boost::ut;
    const auto result = registry.begin_draft(graph, std::move(divergent), supports);
    expect(!result.has_value());
    if (!result) {
        expect(std::ranges::any_of(result.error(), [](const auto& error) {
            return error.code == rift::SpaceBuildErrorCode::replicated_schema_mismatch;
        }));
    }
}

inline rift::PhaseReference divergent_graph_reference(rift::PhaseReference phase, const unsigned int rank)
{
    if (rank == 1U) {
        phase.graph.graph = rift::PhaseGraphInstanceId::from_index(phase.graph.graph.value() + 1);
    }
    return phase;
}

inline rift::PhaseReference divergent_run_reference(rift::PhaseReference phase, const unsigned int rank)
{
    if (rank == 1U) {
        phase.graph.run = rift::RunConfigurationId::from_index(phase.graph.run.value() + 1);
    }
    return phase;
}

inline rift::PhaseReference divergent_phase_reference(rift::PhaseReference phase, const unsigned int rank)
{
    if (rank == 1U) {
        phase.phase = rift::PhaseId::from_index(phase.phase.value() + 1);
    }
    return phase;
}

template<int dim> void check_distributed_agreement()
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
    auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_WORLD);
    dealii::GridGenerator::subdivided_hyper_cube(*triangulation, 2);

    rift::SupportEnvelope owned;
    for (const auto& cell : triangulation->active_cell_iterators()) {
        if (cell->is_locally_owned()) {
            owned.insert(cell->id());
        }
    }
    std::unique_ptr<dealii::Triangulation<dim>> consumed = std::move(triangulation);
    auto mesh = rift::make_mesh_snapshot(run, std::move(consumed)).value();
    rift::SpaceRegistry<dim> const registry(mesh);
    auto fields = rank_permuted_fields(
        std::vector<rift::PhaseFieldGroupSpecification>{
            {.phase = gas, .name = "density", .components = 1, .polynomial_degree = 1},
            {.phase = gas, .name = "momentum", .components = dim, .polynomial_degree = 1}},
        rank);
    rift::SpaceSpecification specification{
        .phase_fields = std::move(fields),
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };
    const std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = gas, .mesh = mesh->id(), .locally_owned_requested_cells = owned}};
    auto draft = registry.begin_draft(graph, std::move(specification), supports);
    expect(draft.has_value());
    if (!draft) {
        return;
    }

    const auto provenance = draft->provenance();
    const std::array identities{provenance.run.value(), provenance.graph.value(), provenance.mesh.value(),
                                provenance.registry.value(), provenance.epoch.value()};
    for (const auto identity : identities) {
        const auto minimum = dealii::Utilities::MPI::min(identity, MPI_COMM_WORLD);
        const auto maximum = dealii::Utilities::MPI::max(identity, MPI_COMM_WORLD);
        expect(minimum == maximum);
    }
    auto field = draft->field_spaces().begin();
    expect(field->name() == "density");
    expect(field->id() == rift::FieldGroupId::from_index(0));
    ++field;
    expect(field->name() == "momentum");
    expect(field->id() == rift::FieldGroupId::from_index(1));
    for (const auto& field : draft->field_spaces()) {
        for (const auto& cell : field.dof_handler().active_cell_iterators()) {
            if (!cell->is_artificial()) {
                expect(cell->active_fe_index() == 0_u);
            }
        }
    }
    auto snapshot = registry.finalize(*draft, {{"pressure"}});
    expect(snapshot.has_value());
    if (!snapshot) {
        return;
    }
    expect(snapshot->provenance() == snapshot->layout().provenance());
    auto block = snapshot->layout().field_blocks().begin();
    expect(block->name == "density");
    ++block;
    expect(block->name == "momentum");
    expect(snapshot->layout().regional_entries().front().locally_owned_entries.n_elements() ==
           static_cast<dealii::types::global_dof_index>(rank_zero_value(rank, 1U, 0U)));

    rift::SpaceSpecification mismatched{
        .phase_fields = {{.phase = gas,
                          .name = rank_one_value(rank, std::string{"temperature"}, std::string{"energy"}),
                          .components = 1,
                          .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };
    const auto rejected = registry.begin_draft(graph, std::move(mismatched), supports);
    expect(!rejected.has_value());
    if (rejected) {
        return;
    }
    expect(std::ranges::any_of(rejected.error(), [](const auto& error) {
        return error.code == rift::SpaceBuildErrorCode::replicated_schema_mismatch;
    }));
    std::string diagnostic;
    for (const auto& error : rejected.error()) {
        diagnostic += std::to_string(static_cast<unsigned int>(error.code)) + ":" + error.message + "\n";
    }
    const auto diagnostics = dealii::Utilities::MPI::all_gather(MPI_COMM_WORLD, diagnostic);
    expect(std::ranges::all_of(diagnostics, [&](const auto& candidate) { return candidate == diagnostic; }));

    rift::SpaceSpecification regional_schema{
        .phase_fields = {{.phase = gas, .name = "regional_flow", .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "regional_level_sets", .components = 1, .polynomial_degree = 1},
    };
    std::vector<rift::PhaseSupportSpecification> regional_supports{
        {.phase = gas, .mesh = mesh->id(), .locally_owned_requested_cells = owned}};
    auto regional_draft = registry.begin_draft(graph, std::move(regional_schema), std::move(regional_supports));
    expect(regional_draft.has_value());
    if (!regional_draft) {
        return;
    }
    const auto regional_rejected = registry.finalize(
        *regional_draft, {{rank_one_value(rank, std::string{"rank_one_region"}, std::string{"rank_zero_region"})}});
    expect(!regional_rejected.has_value());
    expect(has_error_code(regional_rejected, rift::SpaceBuildErrorCode::replicated_schema_mismatch));
    expect(regional_draft->active());
    const auto regional_recovered = registry.finalize(*regional_draft, {{"shared_region"}});
    expect(regional_recovered.has_value());
    expect(!regional_draft->active());

    expect_schema_mismatch(
        registry, graph, supports,
        {.phase_fields =
             {{.phase = gas, .name = "components", .components = rank_one_value(rank, 2U, 1U), .polynomial_degree = 1}},
         .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1}});
    expect_schema_mismatch(
        registry, graph, supports,
        {.phase_fields =
             {{.phase = gas, .name = "degree", .components = 1, .polynomial_degree = rank_one_value(rank, 2U, 1U)}},
         .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1}});
    expect_schema_mismatch(
        registry, graph, supports,
        {.phase_fields = {{.phase = gas, .name = "level_name", .components = 1, .polynomial_degree = 1}},
         .level_set = {.name = rank_one_value(rank, std::string{"other_level_sets"}, std::string{"level_sets"}),
                       .components = 1,
                       .polynomial_degree = 1}});
    expect_schema_mismatch(
        registry, graph, supports,
        {.phase_fields = {{.phase = gas, .name = "level_components", .components = 1, .polynomial_degree = 1}},
         .level_set = {.name = "level_sets", .components = rank_one_value(rank, 2U, 1U), .polynomial_degree = 1}});
    expect_schema_mismatch(
        registry, graph, supports,
        {.phase_fields = {{.phase = gas, .name = "level_degree", .components = 1, .polynomial_degree = 1}},
         .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = rank_one_value(rank, 2U, 1U)}});
    expect_schema_mismatch(registry, graph, supports,
                           {.phase_fields = {{.phase = divergent_graph_reference(gas, rank),
                                              .name = "phase_graph",
                                              .components = 1,
                                              .polynomial_degree = 1}},
                            .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1}});
    expect_schema_mismatch(registry, graph, supports,
                           {.phase_fields = {{.phase = divergent_run_reference(gas, rank),
                                              .name = "phase_run",
                                              .components = 1,
                                              .polynomial_degree = 1}},
                            .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1}});
    expect_schema_mismatch(registry, graph, supports,
                           {.phase_fields = {{.phase = divergent_phase_reference(gas, rank),
                                              .name = "phase_id",
                                              .components = 1,
                                              .polynomial_degree = 1}},
                            .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1}});

    rift::SpaceSpecification support_schema{
        .phase_fields = {{.phase = gas, .name = "support_validation", .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1}};
    auto wrong_mesh_supports = supports;
    wrong_mesh_supports.front().mesh = rank_one_value(rank, rift::MeshSnapshotId::from_index(mesh->id().value() + 1),
                                                      wrong_mesh_supports.front().mesh);
    const auto wrong_mesh = registry.begin_draft(graph, support_schema, std::move(wrong_mesh_supports));
    expect(!wrong_mesh.has_value());
    expect(has_error_code(wrong_mesh, rift::SpaceBuildErrorCode::support_mesh_mismatch));
    auto asymmetric_supports = rank_one_value(rank, std::vector<rift::PhaseSupportSpecification>{},
                                              std::vector<rift::PhaseSupportSpecification>{supports});
    const auto asymmetric = registry.begin_draft(graph, std::move(support_schema), std::move(asymmetric_supports));
    expect(!asymmetric.has_value());
    expect(has_error_code(asymmetric, rift::SpaceBuildErrorCode::missing_phase_support));

    rift::SupportEnvelope nonowned;
    for (const auto& cell : mesh->triangulation().active_cell_iterators()) {
        if (cell->is_ghost()) {
            nonowned.insert(cell->id());
            break;
        }
    }
    expect(!nonowned.empty());
    rift::SpaceSpecification valid{
        .phase_fields = {{.phase = gas, .name = "flow", .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };
    std::vector<rift::PhaseSupportSpecification> invalid_support{
        {.phase = gas, .mesh = mesh->id(), .locally_owned_requested_cells = nonowned}};
    const auto nonowner_result = registry.begin_draft(graph, std::move(valid), std::move(invalid_support));
    expect(!nonowner_result.has_value());
    expect(has_error_code(nonowner_result, rift::SpaceBuildErrorCode::nonowned_support_cell));
}

inline void register_tests()
{
    using namespace boost::ut;
    "distributed schemas, epochs, diagnostics, and ghost FE indices agree in 2D and 3D"_test = [] {
        check_distributed_agreement<2>();
        check_distributed_agreement<3>();
    };
}

} // namespace rift_test::mpi::space_registry_distributed_agreement_00
