#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>

template<int dim> void check_independent_groups()
{
    using namespace boost::ut;

    auto mesh = rift::test::make_two_cell_mesh<dim>();
    const auto ids = rift::test::active_cell_ids(*mesh);
    const auto graph = rift::test::make_single_phase_graph();
    const auto gas = graph.find_phase("gas").value();
    const rift::SupportEnvelope envelope(ids.begin(), ids.end());

    rift::SpaceSpecification specification{
        .phase_fields = {{gas, "species", 2, 1, envelope}, {gas, "flow", dim + 2, 2, envelope}},
        .level_set = {"level_sets", 1, 1},
    };
    rift::SpaceRegistry<dim> registry(mesh, MPI_COMM_SELF);
    auto draft = registry.begin_draft(graph, std::move(specification));

    expect(draft.has_value());
    auto snapshot = registry.finalize(std::move(draft).value(), {});
    expect(snapshot.has_value());
    expect(snapshot->field_spaces().size() == 2_u);

    const auto flow = snapshot->find_field(gas, "flow").value();
    const auto species = snapshot->find_field(gas, "species").value();
    expect(flow != species);
    expect(&snapshot->field_space(gas, flow).dof_handler() != &snapshot->field_space(gas, species).dof_handler());
}

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    using namespace boost::ut;

    "each phase field group owns an independent DoFHandler in 2D and 3D"_test = [] {
        check_independent_groups<2>();
        check_independent_groups<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
