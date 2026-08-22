#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>

namespace {

template<int dim> void check_initial_state()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>({{"pressure"}});
    rift::StateStore const store(space.layout());
    const auto accepted = store.snapshot(rift::StateSlot::accepted);

    expect(accepted.stamp().space == space.epoch());
    expect(accepted.stamp().published_epoch.has_value());
    expect(accepted.field(space.field_spaces().front().id()).l2_norm() == 0.0_d);
    expect(accepted.field(space.level_set_space().id()).l2_norm() == 0.0_d);
    expect(accepted.regional(rift::RegionalEntryId::from_index(0)).l2_norm() == 0.0_d);
}

} // namespace

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize const mpi(argc, argv, 1);
    using namespace boost::ut;

    "a new store publishes a zero-initialized accepted snapshot in 2D and 3D"_test = [] {
        check_initial_state<2>();
        check_initial_state<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
