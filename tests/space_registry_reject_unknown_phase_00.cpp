#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <rift/phase_graph.hpp>
#include <utility>

namespace {

template<int dim> void check_unknown_phase_rejected()
{
    using namespace boost::ut;

    auto mesh = rift::test::make_two_cell_mesh<dim>();
    const auto ids = rift::test::active_cell_ids(*mesh);
    const auto graph = rift::test::make_single_phase_graph();
    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = rift::PhaseId::from_index(12),
                          .name = "flow",
                          .components = 1,
                          .polynomial_degree = 1,
                          .support_envelope = {ids.front()}}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };

    rift::SpaceRegistry<dim> const registry(mesh, MPI_COMM_SELF);
    const auto result = registry.begin_draft(graph, std::move(specification));

    expect(!result.has_value());
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::unknown_phase));
}

} // namespace

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize const mpi(argc, argv, 1);
    using namespace boost::ut;

    "field groups must refer to a phase in the graph in 2D and 3D"_test = [] {
        check_unknown_phase_rejected<2>();
        check_unknown_phase_rejected<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
