#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>

template<int dim> void check_duplicate_group_rejected()
{
    using namespace boost::ut;

    auto mesh = rift::test::make_two_cell_mesh<dim>();
    const auto ids = rift::test::active_cell_ids(*mesh);
    const auto graph = rift::test::make_single_phase_graph();
    const auto gas = graph.find_phase("gas").value();
    rift::SpaceSpecification specification{
        .phase_fields = {{gas, "flow", 1, 1, {ids.front()}}, {gas, "flow", 2, 2, {ids.back()}}},
        .level_set = {"level_sets", 1, 1},
    };

    rift::SpaceRegistry<dim> registry(mesh, MPI_COMM_SELF);
    const auto result = registry.begin_draft(graph, std::move(specification));

    expect(!result.has_value());
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::duplicate_field_name));
}

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    using namespace boost::ut;

    "one phase may not declare the same field-group name twice in 2D or 3D"_test = [] {
        check_duplicate_group_rejected<2>();
        check_duplicate_group_rejected<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
