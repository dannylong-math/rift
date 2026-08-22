#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <stdexcept>

template<int dim> void check_inactive_regional_access()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>({{"pressure"}});
    rift::StateStore store(space.layout());
    auto transaction = store.begin_trial(store.snapshot(rift::StateSlot::accepted).stamp().snapshot);
    transaction.abandon();

    bool rejected = false;
    try {
        static_cast<void>(transaction.regional(rift::RegionalEntryId::from_index(0)));
    }
    catch (const std::logic_error&) {
        rejected = true;
    }
    expect(rejected);
}

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    using namespace boost::ut;

    "regional access is rejected after a transaction becomes inactive in 2D and 3D"_test = [] {
        check_inactive_regional_access<2>();
        check_inactive_regional_access<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
