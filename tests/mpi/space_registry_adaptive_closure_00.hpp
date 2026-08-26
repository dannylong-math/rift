#pragma once

#include <algorithm>
#include <boost/ut.hpp>
#include <cmath>
#include <cstddef>
#include <deal.II/base/mpi.h>
#include <deal.II/base/point.h>
#include <deal.II/base/types.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/fe/mapping_q1.h>
#include <deal.II/grid/cell_id.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <memory>
#include <mpi.h>
#include <optional>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace rift_test::mpi::space_registry_adaptive_closure_00 {

template<int dim> std::unique_ptr<dealii::Triangulation<dim>> make_reference_mesh()
{
    auto reference = std::make_unique<dealii::Triangulation<dim>>();
    std::vector<unsigned int> repetitions(dim, 1);
    repetitions.front() = 2;
    dealii::Point<dim> upper;
    for (unsigned int direction = 0; std::cmp_less(direction, dim); ++direction) {
        upper(direction) = 1.0;
    }
    dealii::GridGenerator::subdivided_hyper_rectangle(*reference, repetitions, dealii::Point<dim>(), upper);
    for (const auto& cell : reference->active_cell_iterators()) {
        if (cell->center()(0) > 0.5) {
            cell->set_refine_flag();
        }
    }
    reference->execute_coarsening_and_refinement();
    return reference;
}

template<int dim> rift::SupportEnvelope expected_closed_mask(const dealii::Triangulation<dim>& reference)
{
    rift::SupportEnvelope expected;
    for (const auto& cell : reference.active_cell_iterators()) {
        if (cell->center()(0) < 0.5 || std::abs(cell->face(0)->center()(0) - 0.5) < 1.0e-14) {
            expected.insert(cell->id());
        }
    }
    return expected;
}

struct ReferenceOracle {
    rift::SupportEnvelope closed;
    std::optional<dealii::CellId> seed;
    std::optional<dealii::CellId> coarse;
};

template<int dim> ReferenceOracle make_reference_oracle(const dealii::Triangulation<dim>& reference)
{
    ReferenceOracle result{.closed = expected_closed_mask(reference), .seed = std::nullopt, .coarse = std::nullopt};
    for (const auto& cell : reference.active_cell_iterators()) {
        if (cell->center()(0) < 0.5) {
            result.coarse = cell->id();
        }
        bool selected = std::abs(cell->face(0)->center()(0) - 0.5) < 1.0e-14;
        for (unsigned int direction = 1; std::cmp_less(direction, dim); ++direction) {
            selected = selected && cell->center()(direction) < 0.5;
        }
        if (selected) {
            result.seed = cell->id();
        }
    }
    return result;
}

template<int dim> struct PreparedDistributedMesh {
    std::unique_ptr<dealii::Triangulation<dim>> triangulation;
    std::optional<dealii::CellId> stale;
    rift::SupportEnvelope requested;
};

struct ExpectedInterfaceCells {
    dealii::CellId seed;
    dealii::CellId coarse;
};

template<int dim> PreparedDistributedMesh<dim> prepare_distributed_mesh(const ExpectedInterfaceCells& expected)
{
    using namespace boost::ut;
    auto distributed = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_WORLD);
    std::vector<unsigned int> repetitions(dim, 1);
    repetitions.front() = 2;
    dealii::Point<dim> upper;
    for (unsigned int direction = 0; std::cmp_less(direction, dim); ++direction) {
        upper(direction) = 1.0;
    }
    dealii::GridGenerator::subdivided_hyper_rectangle(*distributed, repetitions, dealii::Point<dim>(), upper);
    std::optional<dealii::CellId> stale;
    for (const auto& cell : distributed->active_cell_iterators()) {
        if (cell->is_locally_owned() && cell->center()(0) > 0.5) {
            bool selected_parent = true;
            for (unsigned int direction = 1; std::cmp_less(direction, dim); ++direction) {
                selected_parent = selected_parent && cell->center()(direction) <= 0.5;
            }
            if (selected_parent) {
                stale = cell->id();
            }
            cell->set_refine_flag();
        }
    }
    distributed->execute_coarsening_and_refinement();

    rift::SupportEnvelope requested;
    for (const auto& cell : distributed->active_cell_iterators()) {
        if (!cell->is_locally_owned()) {
            continue;
        }
        bool selected = std::abs(cell->face(0)->center()(0) - 0.5) < 1.0e-14;
        for (unsigned int direction = 1; std::cmp_less(direction, dim); ++direction) {
            selected = selected && cell->center()(direction) < 0.5;
        }
        if (selected) {
            requested.insert(cell->id());
        }
    }
    expect(dealii::Utilities::MPI::sum(static_cast<unsigned int>(requested.size()), MPI_COMM_WORLD) == 1_u);
    const auto rank = dealii::Utilities::MPI::this_mpi_process(MPI_COMM_WORLD);
    const auto local_seed_owner = requested.contains(expected.seed) ? static_cast<int>(rank) : -1;
    int local_coarse_owner = -1;
    for (const auto& cell : distributed->active_cell_iterators()) {
        if (cell->is_locally_owned() && cell->id() == expected.coarse) {
            local_coarse_owner = static_cast<int>(rank);
        }
    }
    const auto seed_owner = dealii::Utilities::MPI::max(local_seed_owner, MPI_COMM_WORLD);
    const auto coarse_owner = dealii::Utilities::MPI::max(local_coarse_owner, MPI_COMM_WORLD);
    expect(seed_owner >= 0_i);
    expect(coarse_owner >= 0_i);
    expect(seed_owner != coarse_owner);
    return {.triangulation = std::move(distributed), .stale = stale, .requested = std::move(requested)};
}

template<int dim> rift::SupportEnvelope gather_mask(const rift::SupportEnvelope& local)
{
    std::vector<std::string> local_ids;
    for (const auto& id : local) {
        local_ids.push_back(id.to_string());
    }
    rift::SupportEnvelope global;
    for (const auto& rank_ids : dealii::Utilities::MPI::all_gather(MPI_COMM_WORLD, local_ids)) {
        for (const auto& id : rank_ids) {
            global.emplace(id);
        }
    }
    return global;
}

template<int dim> void check_constraint_reproduction(const rift::FieldGroupSpace<dim>& field)
{
    using namespace boost::ut;
    const auto points = dealii::DoFTools::map_dofs_to_support_points(dealii::MappingQ1<dim>(), field.dof_handler());
    const auto local_constraints = static_cast<unsigned long long>(field.constraints().n_constraints());
    const auto global_constraints = dealii::Utilities::MPI::sum(local_constraints, MPI_COMM_WORLD);
    expect(global_constraints > 0_u);
    bool local_hanging_interface_constraint = false;
    for (const auto& line : field.constraints().get_lines()) {
        const auto& point = points.at(line.index);
        bool hanging_point = false;
        for (unsigned int direction = 1; std::cmp_less(direction, dim); ++direction) {
            hanging_point = hanging_point || (point(direction) > 0.0 && point(direction) < 1.0);
        }
        local_hanging_interface_constraint =
            local_hanging_interface_constraint ||
            (std::abs(point(0) - 0.5) < 1.0e-14 && hanging_point && !line.entries.empty());
    }
    const auto global_hanging_interface_constraint =
        dealii::Utilities::MPI::max(local_hanging_interface_constraint ? 1 : 0, MPI_COMM_WORLD);
    expect(global_hanging_interface_constraint == 1_i);
    for (int component = -1; component < dim; ++component) {
        const auto value = [&](const dealii::types::global_dof_index dof) {
            const auto& point = points.at(dof);
            return component < 0 ? 1.0 : point(static_cast<unsigned int>(component));
        };
        for (const auto& line : field.constraints().get_lines()) {
            double constrained = line.inhomogeneity;
            for (const auto& [master, weight] : line.entries) {
                constrained += weight * value(master);
            }
            expect(std::abs(constrained - value(line.index)) < 1.0e-13);
        }
    }
}

template<int dim> void check_distributed_adaptive_closure()
{
    using namespace boost::ut;

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
    const auto oracle = make_reference_oracle(*make_reference_mesh<dim>());
    constexpr std::size_t expected_size = dim == 2 ? 3 : 5;
    expect(oracle.closed.size() == expected_size);
    expect(oracle.seed.has_value());
    expect(oracle.coarse.has_value());
    if (!oracle.seed || !oracle.coarse) {
        return;
    }
    auto prepared = prepare_distributed_mesh<dim>({.seed = *oracle.seed, .coarse = *oracle.coarse});
    auto mesh = rift::make_mesh_snapshot(run, std::move(prepared.triangulation)).value();
    rift::SpaceRegistry<dim> const registry(mesh);
    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = gas, .name = "flow", .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };
    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = gas, .mesh = mesh->id(), .locally_owned_requested_cells = prepared.requested}};
    auto draft = registry.begin_draft(graph, std::move(specification), std::move(supports));
    expect(draft.has_value());
    if (!draft) {
        return;
    }

    const auto& support = draft->field_spaces().front().support();
    const auto local_added = static_cast<unsigned int>(support.closure_added_locally_owned_cells().size());
    const auto local_final = static_cast<unsigned int>(support.final_locally_owned_cells().size());
    const auto global_added = dealii::Utilities::MPI::sum(local_added, MPI_COMM_WORLD);
    const auto global_final = dealii::Utilities::MPI::sum(local_final, MPI_COMM_WORLD);
    constexpr unsigned int expected_added_count = dim == 2 ? 2U : 4U;
    constexpr unsigned int expected_final_count = dim == 2 ? 3U : 5U;
    constexpr dealii::types::global_dof_index expected_field_dofs = dim == 2 ? 8 : 22;
    constexpr dealii::types::global_dof_index expected_level_set_dofs = dim == 2 ? 11 : 31;
    expect(global_added == expected_added_count);
    expect(global_final == expected_final_count);
    expect(draft->field_spaces().front().dof_handler().n_dofs() == expected_field_dofs);
    expect(draft->level_set_space().dof_handler().n_dofs() == expected_level_set_dofs);

    const auto global_final_mask = gather_mask<dim>(support.final_locally_owned_cells());
    expect(global_final_mask == oracle.closed);
    const auto global_added_mask = gather_mask<dim>(support.closure_added_locally_owned_cells());
    auto expected_added = oracle.closed;
    expected_added.erase(*oracle.seed);
    expect(global_added_mask == expected_added);
    expect(global_added_mask.contains(*oracle.coarse));
    rift::SupportEnvelope expected_local;
    for (const auto& cell : mesh->triangulation().active_cell_iterators()) {
        if (cell->is_locally_owned() && oracle.closed.contains(cell->id())) {
            expected_local.insert(cell->id());
        }
    }
    expect(support.final_locally_owned_cells() == expected_local);
    const unsigned int local_participation = expected_local.empty() ? 0U : 1U;
    const auto participation = dealii::Utilities::MPI::all_gather(MPI_COMM_WORLD, local_participation);
    const auto size = dealii::Utilities::MPI::n_mpi_processes(MPI_COMM_WORLD);
    expect(std::ranges::count(participation, 1U) >= 2);
    if (size == 3U) {
        expect(participation.at(1) == 1_u);
    }
    for (const auto& cell : draft->field_spaces().front().dof_handler().active_cell_iterators()) {
        if (!cell->is_artificial()) {
            expect(cell->active_fe_index() ==
                   static_cast<unsigned int>(global_final_mask.contains(cell->id()) ? 0 : 1));
        }
    }
    check_constraint_reproduction(draft->field_spaces().front());

    rift::SpaceSpecification stale_specification{
        .phase_fields = {{.phase = gas, .name = "stale", .components = 1, .polynomial_degree = 1}},
        .level_set = {.name = "stale_level_sets", .components = 1, .polynomial_degree = 1}};
    rift::SupportEnvelope stale_requested;
    if (prepared.stale) {
        stale_requested.insert(*prepared.stale);
    }
    std::vector<rift::PhaseSupportSpecification> stale_supports{
        {.phase = gas, .mesh = mesh->id(), .locally_owned_requested_cells = std::move(stale_requested)}};
    const auto stale = registry.begin_draft(graph, std::move(stale_specification), std::move(stale_supports));
    expect(!stale.has_value());
    if (stale) {
        return;
    }
    expect(std::ranges::any_of(stale.error(), [](const auto& error) {
        return error.code == rift::SpaceBuildErrorCode::inactive_support_cell;
    }));
}

inline void register_tests()
{
    using namespace boost::ut;
    "two-rank hanging closure returns requests to 2D and 3D cell owners"_test = [] {
        check_distributed_adaptive_closure<2>();
        check_distributed_adaptive_closure<3>();
    };
}

} // namespace rift_test::mpi::space_registry_adaptive_closure_00
