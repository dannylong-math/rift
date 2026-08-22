#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>

template<int dim> void check_space_epoch()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    const rift::StateStore store(space.layout());

    expect(store.space_epoch() == space.epoch());
}

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    using namespace boost::ut;

    "the state store reports its finalized space epoch in 2D and 3D"_test = [] {
        check_space_epoch<2>();
        check_space_epoch<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
