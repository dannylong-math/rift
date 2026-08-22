#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <stdexcept>

template<int dim> void check_previous_missing()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    rift::StateStore store(space.layout());

    bool rejected = false;
    try {
        static_cast<void>(store.snapshot(rift::StateSlot::previous));
    }
    catch (const std::out_of_range&) {
        rejected = true;
    }
    expect(rejected);
}

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    using namespace boost::ut;

    "a new store has no previous accepted snapshot in 2D and 3D"_test = [] {
        check_previous_missing<2>();
        check_previous_missing<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
