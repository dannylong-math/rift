#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>

template<int dim> void check_full_background_level_set()
{
    using namespace boost::ut;

    auto mesh = rift::test::make_two_cell_mesh<dim>();
    const auto ids = rift::test::active_cell_ids(*mesh);
    const auto graph = rift::test::make_single_phase_graph();
    const auto gas = graph.find_phase("gas").value();

    rift::SpaceSpecification specification{
        .phase_fields = {{gas, "flow", 1, 1, {ids.front()}}},
        .level_set = {"level_sets", 2, 1},
    };
    rift::SpaceRegistry<dim> registry(mesh, MPI_COMM_SELF);
    auto draft = registry.begin_draft(graph, std::move(specification));
    auto snapshot = registry.finalize(std::move(draft).value(), {});

    for (const auto& cell : snapshot->level_set_space().dof_handler().active_cell_iterators()) {
        expect(cell->active_fe_index() == 0_u);
        expect(cell->get_fe().dofs_per_cell > 0_u);
    }
}

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    using namespace boost::ut;

    "the level-set field group covers the complete 2D and 3D background mesh"_test = [] {
        check_full_background_level_set<2>();
        check_full_background_level_set<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
