#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>
#include <utility>

namespace {

template<int dim> void check_independent_groups()
{
    using namespace boost::ut;

    auto mesh = rift::test::make_two_cell_mesh<dim>();
    const auto ids = rift::test::active_cell_ids(*mesh);
    const auto graph = rift::test::make_single_phase_graph();
    const auto gas = rift::test::require_optional(graph.find_phase("gas"));
    const rift::SupportEnvelope envelope(ids.begin(), ids.end());

    rift::SpaceSpecification specification{
        .phase_fields =
            {{.phase = gas, .name = "species", .components = 2, .polynomial_degree = 1, .support_envelope = envelope},
             {.phase = gas,
              .name = "flow",
              .components = dim + 2,
              .polynomial_degree = 2,
              .support_envelope = envelope}},
        .level_set = {.name = "level_sets", .components = 1, .polynomial_degree = 1},
    };
    rift::SpaceRegistry<dim> const registry(mesh, MPI_COMM_SELF);
    auto draft = registry.begin_draft(graph, std::move(specification));

    expect(draft.has_value());
    auto snapshot = registry.finalize(std::move(draft).value(), {});
    expect(snapshot.has_value());
    expect(snapshot->field_spaces().size() == 2_u);

    const auto flow = rift::test::require_optional(snapshot->find_field(gas, "flow"));
    const auto species = rift::test::require_optional(snapshot->find_field(gas, "species"));
    expect(flow != species);
    expect(&snapshot->field_space(gas, flow).dof_handler() != &snapshot->field_space(gas, species).dof_handler());
}

} // namespace

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize const mpi(argc, argv, 1);
    using namespace boost::ut;

    "each phase field group owns an independent DoFHandler in 2D and 3D"_test = [] {
        check_independent_groups<2>();
        check_independent_groups<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
