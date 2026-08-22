#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>

template<int dim> void check_regional_state()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>({{"pressure"}});
    rift::StateStore store(space.layout());
    const auto initial = store.snapshot(rift::StateSlot::accepted);
    const auto pressure = space.layout().regional_entries().front().id;

    auto transaction = store.begin_trial(initial.stamp().snapshot);
    transaction.regional(pressure) = 8.0;
    const auto candidate = transaction.seal();

    expect(initial.regional(pressure).l2_norm() == 0.0_d);
    expect(candidate.regional(pressure).l2_norm() == 8.0_d);
}

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    using namespace boost::ut;

    "regional unknowns are owned and versioned with field blocks in 2D and 3D"_test = [] {
        check_regional_state<2>();
        check_regional_state<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
