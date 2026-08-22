#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <deal.II/fe/fe_data.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <utility>

namespace {

template<int dim> void check_non_dominating_nothing()
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

    expect(space.dof_handler().get_fe(1).compare_for_domination(space.dof_handler().get_fe(0), 1) ==
           dealii::FiniteElementDomination::no_requirements);
    expect(space.constraints().n_constraints() == 0_u);
}

} // namespace

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize const mpi(argc, argv, 1);
    using namespace boost::ut;

    "FE_Nothing does not constrain the active phase trace in 2D and 3D"_test = [] {
        check_non_dominating_nothing<2>();
        check_non_dominating_nothing<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
