#pragma once

#include <boost/ut.hpp>
#include <cstddef>
#include <deal.II/base/mpi.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <memory>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace rift_test::mpi::space_registry_disjoint_multicomponent_00 {

struct PartitionedSupport {
    rift::SupportEnvelope gas_local;
    rift::SupportEnvelope liquid_local;
    rift::SupportEnvelope gas_global;
    rift::SupportEnvelope liquid_global;
};

inline rift::PhaseReference require_phase(const rift::PhaseGraph& graph, const std::string_view name)
{
    const auto id = graph.find_phase(name);
    if (!id.has_value()) {
        throw std::logic_error("the disjoint-space fixture did not contain a required phase");
    }
    return graph.reference(*id).value();
}

template<int dim> PartitionedSupport partition_support(const dealii::Triangulation<dim>& triangulation)
{
    PartitionedSupport support;
    for (const auto& cell : triangulation.active_cell_iterators()) {
        if (cell->center()(0) < 0.5) {
            support.gas_global.insert(cell->id());
            if (cell->is_locally_owned()) {
                support.gas_local.insert(cell->id());
            }
        }
        else {
            support.liquid_global.insert(cell->id());
            if (cell->is_locally_owned()) {
                support.liquid_local.insert(cell->id());
            }
        }
    }
    return support;
}

template<int dim>
void check_field_assignments(const std::span<const rift::FieldGroupSpace<dim>> fields, const rift::PhaseReference gas,
                             const PartitionedSupport& support)
{
    using namespace boost::ut;
    for (const auto& field : fields) {
        const auto& expected = field.phase().phase == gas.phase ? support.gas_global : support.liquid_global;
        for (const auto& cell : field.dof_handler().active_cell_iterators()) {
            if (!cell->is_artificial()) {
                const auto expected_index = static_cast<unsigned int>(expected.contains(cell->id()) ? 0 : 1);
                expect(cell->active_fe_index() == expected_index);
                expect(cell->get_fe().n_components() == field.components());
            }
        }
    }
}

template<int dim> void check_disjoint_multicomponent_spaces()
{
    using namespace boost::ut;
    auto run = rift::RunConfiguration::create(MPI_COMM_WORLD).value();
    auto graph = rift::make_phase_graph(run, {{"gas", "compressible"}, {"liquid", "compressible"}}, {}).value();
    const auto gas = require_phase(graph, "gas");
    const auto liquid = require_phase(graph, "liquid");
    auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(MPI_COMM_WORLD);
    dealii::GridGenerator::subdivided_hyper_cube(*triangulation, 2);
    const auto partitioned = partition_support(*triangulation);
    std::unique_ptr<dealii::Triangulation<dim>> consumed = std::move(triangulation);
    auto mesh = rift::make_mesh_snapshot(run, std::move(consumed)).value();
    rift::SpaceRegistry<dim> const registry(mesh);
    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = gas, .name = "gas_flow", .components = dim + 2, .polynomial_degree = 1},
                         {.phase = gas, .name = "gas_thermo", .components = 2, .polynomial_degree = 1},
                         {.phase = liquid, .name = "liquid_flow", .components = dim + 1, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 2, .polynomial_degree = 1},
    };
    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = gas, .mesh = mesh->id(), .locally_owned_requested_cells = partitioned.gas_local},
        {.phase = liquid, .mesh = mesh->id(), .locally_owned_requested_cells = partitioned.liquid_local}};
    const auto draft = registry.begin_draft(graph, std::move(specification), std::move(supports));
    expect(draft.has_value());
    if (!draft.has_value()) {
        return;
    }
    const auto fields = draft->field_spaces();
    expect(fields.size() == 3_u);
    if (fields.size() != std::size_t{3}) {
        return;
    }
    auto field = fields.begin();
    const auto& gas_flow = *field;
    ++field;
    const auto& gas_thermo = *field;
    ++field;
    const auto& liquid_flow = *field;
    expect(&gas_flow.support() == &gas_thermo.support());
    expect(&gas_flow.support() != &liquid_flow.support());
    check_field_assignments(draft->field_spaces(), gas, partitioned);
}

inline void register_tests()
{
    using namespace boost::ut;
    "disjoint partial phase masks synchronize multicomponent FE Nothing in 2D and 3D"_test = [] {
        check_disjoint_multicomponent_spaces<2>();
        check_disjoint_multicomponent_spaces<3>();
    };
}

} // namespace rift_test::mpi::space_registry_disjoint_multicomponent_00
