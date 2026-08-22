#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <deal.II/grid/cell_id.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <utility>

namespace {

template<int dim> void check_unknown_cell_rejected()
{
    using namespace boost::ut;

    auto mesh = rift::test::make_two_cell_mesh<dim>();
    const auto graph = rift::test::make_single_phase_graph();
    const auto gas = rift::test::require_optional(graph.find_phase("gas"));
    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = gas,
                          .name = "flow",
                          .components = 1,
                          .polynomial_degree = 1,
                          .support_envelope = {dealii::CellId("99_0:")}}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };

    rift::SpaceRegistry<dim> const registry(mesh, MPI_COMM_SELF);
    const auto result = registry.begin_draft(graph, std::move(specification));

    expect(!result.has_value());
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::unknown_support_cell));
}

} // namespace

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize const mpi(argc, argv, 1);
    using namespace boost::ut;

    "support envelopes reject cells outside the background mesh in 2D and 3D"_test = [] {
        check_unknown_cell_rejected<2>();
        check_unknown_cell_rejected<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
