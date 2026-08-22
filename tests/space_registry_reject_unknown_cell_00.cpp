#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>

template<int dim> void check_unknown_cell_rejected()
{
    using namespace boost::ut;

    auto mesh = rift::test::make_two_cell_mesh<dim>();
    const auto graph = rift::test::make_single_phase_graph();
    const auto gas = graph.find_phase("gas").value();
    rift::SpaceSpecification specification{
        .phase_fields = {{gas, "flow", 1, 1, {dealii::CellId("99_0:")}}},
        .level_set = {"level_sets", 1, 1},
    };

    rift::SpaceRegistry<dim> registry(mesh, MPI_COMM_SELF);
    const auto result = registry.begin_draft(graph, std::move(specification));

    expect(!result.has_value());
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::unknown_support_cell));
}

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    using namespace boost::ut;

    "support envelopes reject cells outside the background mesh in 2D and 3D"_test = [] {
        check_unknown_cell_rejected<2>();
        check_unknown_cell_rejected<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
