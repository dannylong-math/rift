#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <utility>

namespace {

template<int dim> void check_duplicate_group_rejected()
{
    using namespace boost::ut;

    auto mesh = rift::test::make_two_cell_mesh<dim>();
    const auto ids = rift::test::active_cell_ids(*mesh);
    const auto graph = rift::test::make_single_phase_graph();
    const auto gas = rift::test::require_optional(graph.find_phase("gas"));
    rift::SpaceSpecification specification{
        .phase_fields =
            {{.phase = gas, .name = "flow", .components = 1, .polynomial_degree = 1, .support_envelope = {ids.front()}},
             {.phase = gas, .name = "flow", .components = 2, .polynomial_degree = 2, .support_envelope = {ids.back()}}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };

    rift::SpaceRegistry<dim> const registry(mesh, MPI_COMM_SELF);
    const auto result = registry.begin_draft(graph, std::move(specification));

    expect(!result.has_value());
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::duplicate_field_name));
}

} // namespace

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize const mpi(argc, argv, 1);
    using namespace boost::ut;

    "one phase may not declare the same field-group name twice in 2D or 3D"_test = [] {
        check_duplicate_group_rejected<2>();
        check_duplicate_group_rejected<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
