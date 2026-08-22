#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <utility>

namespace {

template<int dim> void check_uniform_degree()
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
                          .polynomial_degree = 2,
                          .support_envelope = rift::SupportEnvelope(ids.begin(), ids.end())}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 3},
    };
    rift::SpaceRegistry<dim> const registry(mesh, MPI_COMM_SELF);
    auto draft = registry.begin_draft(graph, std::move(specification));
    auto snapshot = registry.finalize(std::move(draft).value(), {});
    const auto& field = snapshot->field_space(gas, rift::test::require_optional(snapshot->find_field(gas, "flow")));

    for (const auto& cell : field.dof_handler().active_cell_iterators()) {
        expect(cell->get_fe().degree == 2_u);
    }
    for (const auto& cell : snapshot->level_set_space().dof_handler().active_cell_iterators()) {
        expect(cell->get_fe().degree == 3_u);
    }
}

} // namespace

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize const mpi(argc, argv, 1);
    using namespace boost::ut;

    "every active cell uses its field group's uniform degree in 2D and 3D"_test = [] {
        check_uniform_degree<2>();
        check_uniform_degree<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
