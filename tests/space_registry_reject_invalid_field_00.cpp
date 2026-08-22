#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <utility>

namespace {

template<int dim> void check_invalid_fields_collected()
{
    using namespace boost::ut;

    auto mesh = rift::test::make_two_cell_mesh<dim>();
    const auto graph = rift::test::make_single_phase_graph();
    const auto gas = rift::test::require_optional(graph.find_phase("gas"));
    rift::SpaceSpecification specification{
        .phase_fields = {{.phase = gas, .name = "", .components = 0, .polynomial_degree = 0, .support_envelope = {}}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };

    rift::SpaceRegistry<dim> const registry(mesh, MPI_COMM_SELF);
    const auto result = registry.begin_draft(graph, std::move(specification));

    expect(!result.has_value());
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::empty_field_name));
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::zero_components));
    expect(rift::test::has_space_error(result.error(), rift::SpaceBuildErrorCode::zero_polynomial_degree));
}

} // namespace

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize const mpi(argc, argv, 1);
    using namespace boost::ut;

    "independent field-schema errors are collected in 2D and 3D"_test = [] {
        check_invalid_fields_collected<2>();
        check_invalid_fields_collected<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
