#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>

template<int dim> void check_abandon()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    rift::StateStore store(space.layout());
    const auto accepted = store.snapshot(rift::StateSlot::accepted);
    const auto field = space.field_spaces().front().id();

    auto transaction = store.begin_trial(accepted.stamp().snapshot);
    transaction.field(field) = 4.0;
    transaction.abandon();
    transaction.abandon();

    expect(!transaction.active());
    expect(store.snapshot(rift::StateSlot::accepted).field(field).l2_norm() == 0.0_d);
}

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    using namespace boost::ut;

    "abandoning a trial discards its mutable values in 2D and 3D"_test = [] {
        check_abandon<2>();
        check_abandon<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
