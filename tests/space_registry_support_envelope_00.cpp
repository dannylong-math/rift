#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <utility>

namespace {

template<int dim> void check_support_envelope()
{
    using namespace boost::ut;

    auto mesh = rift::test::make_two_cell_mesh<dim>();
    const auto ids = rift::test::active_cell_ids(*mesh);
    const auto graph = rift::test::make_single_phase_graph();
    const auto gas = rift::test::require_optional(graph.find_phase("gas"));

    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = gas,
                          .name = "flow",
                          .components = 1,
                          .polynomial_degree = 1,
                          .support_envelope = {ids.front()}}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };
    rift::SpaceRegistry<dim> const registry(mesh, MPI_COMM_SELF);
    auto draft = registry.begin_draft(graph, std::move(specification));
    auto snapshot = registry.finalize(std::move(draft).value(), {});
    const auto& space = snapshot->field_space(gas, rift::test::require_optional(snapshot->find_field(gas, "flow")));

    for (const auto& cell : space.dof_handler().active_cell_iterators()) {
        if (cell->id() == ids.front()) {
            expect(cell->active_fe_index() == 0_u);
            expect(cell->get_fe().dofs_per_cell > 0_u);
        }
        else {
            expect(cell->active_fe_index() == 1_u);
            expect(cell->get_fe().dofs_per_cell == 0_u);
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize const mpi(argc, argv, 1);
    using namespace boost::ut;

    "phase cells outside the support envelope own no DoFs in 2D and 3D"_test = [] {
        check_support_envelope<2>();
        check_support_envelope<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
