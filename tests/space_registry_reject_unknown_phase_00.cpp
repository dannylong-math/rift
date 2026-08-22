#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>

template<int dim> void check_unknown_phase_rejected()
{
    using namespace boost::ut;

    auto mesh = rift::test::make_two_cell_mesh<dim>();
    const auto ids = rift::test::active_cell_ids(*mesh);
    const auto graph = rift::test::make_single_phase_graph();
    rift::SpaceSpecification specification{
        .phase_fields = {{rift::PhaseId::from_index(12), "flow", 1, 1, {ids.front()}}},
        .level_set = {"level_sets", 1, 1},
    };

    rift::SpaceRegistry<dim> registry(mesh, MPI_COMM_SELF);
    const auto result = registry.begin_draft(graph, std::move(specification));

    expect(!result.has_value());
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::unknown_phase));
}

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    using namespace boost::ut;

    "field groups must refer to a phase in the graph in 2D and 3D"_test = [] {
        check_unknown_phase_rejected<2>();
        check_unknown_phase_rejected<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
