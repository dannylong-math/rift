#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <stdexcept>

template<int dim> void check_unknown_snapshot_rejected()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    rift::StateStore store(space.layout());
    const auto unknown = rift::StateSnapshotId::from_index(999999);

    bool begin_rejected = false;
    try {
        static_cast<void>(store.begin_trial(unknown));
    }
    catch (const std::out_of_range&) {
        begin_rejected = true;
    }

    bool publish_rejected = false;
    try {
        static_cast<void>(store.publish(unknown));
    }
    catch (const std::out_of_range&) {
        publish_rejected = true;
    }

    expect(begin_rejected);
    expect(publish_rejected);
}

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    using namespace boost::ut;

    "unknown snapshot identities are rejected before mutation or publication in 2D and 3D"_test = [] {
        check_unknown_snapshot_rejected<2>();
        check_unknown_snapshot_rejected<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
