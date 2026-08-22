#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>
#include <stdexcept>

namespace {

template<int dim> void check_unknown_discard_rejected()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    rift::StateStore store(space.layout());

    bool rejected = false;
    try {
        store.discard(rift::StateSnapshotId::from_index(999999));
    }
    catch (const std::out_of_range&) {
        rejected = true;
    }
    expect(rejected);
}

} // namespace

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize const mpi(argc, argv, 1);
    using namespace boost::ut;

    "discard rejects an unknown snapshot identity in 2D and 3D"_test = [] {
        check_unknown_discard_rejected<2>();
        check_unknown_discard_rejected<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
