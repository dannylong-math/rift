#pragma once

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <deal.II/base/point.h>
#include <deal.II/grid/cell_id.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <memory>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rift_test::space_registry_phase_support_00 {

template<int dim> void check_shared_phase_support()
{
    using namespace boost::ut;

    auto run = rift::RunConfiguration::create(MPI_COMM_SELF).value();
    auto graph = rift::make_phase_graph(run, {{"gas", "compressible"}}, {}).value();
    const auto gas_id = graph.find_phase("gas");
    if (!gas_id.has_value()) {
        throw std::logic_error("the phase-support fixture did not contain gas");
    }
    const auto gas = graph.reference(*gas_id).value();

    auto triangulation = std::make_unique<dealii::Triangulation<dim>>();
    std::vector<unsigned int> repetitions(dim, 1);
    repetitions.front() = 2;
    dealii::Point<dim> upper;
    for (unsigned int direction = 0; std::cmp_less(direction, dim); ++direction) {
        upper(direction) = 1.0;
    }
    dealii::GridGenerator::subdivided_hyper_rectangle(*triangulation, repetitions, dealii::Point<dim>(), upper);

    std::set<dealii::CellId> requested;
    for (const auto& cell : triangulation->active_cell_iterators()) {
        requested.insert(cell->id());
    }

    auto mesh = rift::make_mesh_snapshot(run, std::move(triangulation)).value();
    rift::SpaceRegistry<dim> const registry(mesh);
    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = gas, .name = "density", .components = 1, .polynomial_degree = 1},
                         {.phase = gas, .name = "momentum", .components = dim, .polynomial_degree = 1}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };
    std::vector<rift::PhaseSupportSpecification> supports{
        {.phase = gas, .mesh = mesh->id(), .locally_owned_requested_cells = requested}};

    auto draft = registry.begin_draft(graph, std::move(specification), std::move(supports));
    expect(draft.has_value());
    expect(draft->field_spaces().size() == 2_u);

    const auto& first = draft->field_spaces().front().support();
    const auto& second = draft->field_spaces().back().support();
    expect(&first == &second);
    expect(first.phase().graph.run == gas.graph.run);
    expect(first.phase().graph.graph == gas.graph.graph);
    expect(first.phase().phase == gas.phase);
    expect(first.mesh_id() == mesh->id());
    expect(first.requested_locally_owned_cells() == requested);
    expect(first.closure_added_locally_owned_cells().empty());
    expect(first.final_locally_owned_cells() == requested);
}

} // namespace rift_test::space_registry_phase_support_00

namespace rift_test::space_registry_phase_support_00 {

inline void register_tests()
{
    using namespace boost::ut;

    "one owner-local support object is shared by all fields of a phase"_test = [] {
        check_shared_phase_support<2>();
        check_shared_phase_support<3>();
    };
}

} // namespace rift_test::space_registry_phase_support_00
