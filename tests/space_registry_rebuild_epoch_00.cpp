#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>

template<int dim> void check_rebuild_epoch()
{
    using namespace boost::ut;

    auto mesh = rift::test::make_two_cell_mesh<dim>();
    const auto ids = rift::test::active_cell_ids(*mesh);
    const auto graph = rift::test::make_single_phase_graph();
    const auto gas = graph.find_phase("gas").value();
    rift::SpaceRegistry<dim> registry(mesh, MPI_COMM_SELF);

    rift::SpaceSpecification first_specification{
        .phase_fields = {{gas, "flow", 1, 1, {ids.front()}}},
        .level_set = {"level_sets", 1, 1},
    };
    auto first_draft = registry.begin_draft(graph, std::move(first_specification));
    auto first = registry.finalize(std::move(first_draft).value(), {});

    rift::SpaceSpecification second_specification{
        .phase_fields = {{gas, "flow", 1, 1, rift::SupportEnvelope(ids.begin(), ids.end())}},
        .level_set = {"level_sets", 1, 1},
    };
    auto second_draft = registry.begin_draft(graph, std::move(second_specification));
    auto second = registry.finalize(std::move(second_draft).value(), {});

    expect(first->epoch() != second->epoch());
    expect(first->field_spaces().front().dof_handler().n_dofs() <
           second->field_spaces().front().dof_handler().n_dofs());
}

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    using namespace boost::ut;

    "support-envelope rebuilds receive a new epoch in 2D and 3D"_test = [] {
        check_rebuild_epoch<2>();
        check_rebuild_epoch<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
