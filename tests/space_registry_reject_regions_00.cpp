#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>

template<int dim> void check_invalid_regions_rejected()
{
    using namespace boost::ut;

    auto mesh = rift::test::make_two_cell_mesh<dim>();
    const auto ids = rift::test::active_cell_ids(*mesh);
    const auto graph = rift::test::make_single_phase_graph();
    const auto gas = graph.find_phase("gas").value();
    rift::SpaceSpecification specification{
        .phase_fields = {{gas, "flow", 1, 1, rift::SupportEnvelope(ids.begin(), ids.end())}},
        .level_set = {"level_sets", 1, 1},
    };
    rift::SpaceRegistry<dim> registry(mesh, MPI_COMM_SELF);
    auto draft = registry.begin_draft(graph, std::move(specification));
    const auto result = registry.finalize(std::move(draft).value(), {{""}, {"pressure"}, {"pressure"}});

    expect(!result.has_value());
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::empty_regional_entry_name));
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::duplicate_regional_entry_name));
}

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    using namespace boost::ut;

    "invalid regional schemas are rejected during 2D and 3D finalization"_test = [] {
        check_invalid_regions_rejected<2>();
        check_invalid_regions_rejected<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
