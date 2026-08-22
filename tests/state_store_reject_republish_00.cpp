#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>
#include <stdexcept>

namespace {

template<int dim> void check_republish_rejected()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    rift::StateStore store(space.layout());
    const auto accepted = store.snapshot(rift::StateSlot::accepted);

    bool rejected = false;
    try {
        static_cast<void>(store.publish(accepted.stamp().snapshot));
    }
    catch (const std::logic_error&) {
        rejected = true;
    }
    expect(rejected);
}

} // namespace

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize const mpi(argc, argv, 1);
    using namespace boost::ut;

    "an already published snapshot cannot be published again in 2D and 3D"_test = [] {
        check_republish_rejected<2>();
        check_republish_rejected<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
