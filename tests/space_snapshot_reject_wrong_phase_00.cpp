#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <stdexcept>

template<int dim> void check_wrong_phase_rejected()
{
    using namespace boost::ut;

    auto mesh = rift::test::make_two_cell_mesh<dim>();
    const auto ids = rift::test::active_cell_ids(*mesh);
    auto graph_result = rift::make_phase_graph({{"gas", "compressible"}, {"liquid", "incompressible"}}, {});
    const auto graph = std::move(graph_result).value();
    const auto gas = graph.find_phase("gas").value();
    const auto liquid = graph.find_phase("liquid").value();

    rift::SpaceSpecification specification{
        .phase_fields = {{gas, "flow", 1, 1, rift::SupportEnvelope(ids.begin(), ids.end())}},
        .level_set = {"level_sets", 1, 1},
    };
    rift::SpaceRegistry<dim> registry(mesh, MPI_COMM_SELF);
    auto draft = registry.begin_draft(graph, std::move(specification));
    auto snapshot = registry.finalize(std::move(draft).value(), {});
    const auto flow = snapshot->find_field(gas, "flow").value();

    bool rejected = false;
    try {
        static_cast<void>(snapshot->field_space(liquid, flow));
    }
    catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected);
}

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    using namespace boost::ut;

    "field identities cannot be resolved through the wrong phase in 2D and 3D"_test = [] {
        check_wrong_phase_rejected<2>();
        check_wrong_phase_rejected<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
