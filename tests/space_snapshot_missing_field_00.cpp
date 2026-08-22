#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
namespace {

template<int dim> void check_missing_field_lookup()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    const auto graph = rift::test::make_single_phase_graph();
    const auto gas = rift::test::require_optional(graph.find_phase("gas"));

    expect(!space.find_field(gas, "temperature").has_value());
}

} // namespace

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize const mpi(argc, argv, 1);
    using namespace boost::ut;

    "an absent phase field name returns no identity in 2D and 3D"_test = [] {
        check_missing_field_lookup<2>();
        check_missing_field_lookup<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
