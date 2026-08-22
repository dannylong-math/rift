#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>

template<int dim> void check_publication()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    rift::StateStore store(space.layout());
    const auto initial = store.snapshot(rift::StateSlot::accepted);
    const auto field = space.field_spaces().front().id();

    auto transaction = store.begin_trial(initial.stamp().snapshot);
    transaction.field(field) = 2.0;
    const auto candidate = transaction.seal();
    const auto accepted = store.publish(candidate.stamp().snapshot);
    const auto previous = store.snapshot(rift::StateSlot::previous);

    expect(accepted.stamp().published_epoch.has_value());
    expect(accepted.stamp().published_epoch != initial.stamp().published_epoch);
    expect(accepted.stamp().snapshot == candidate.stamp().snapshot);
    expect(previous.stamp().snapshot == initial.stamp().snapshot);
    expect(previous.field(field).l2_norm() == 0.0_d);
    expect(accepted.field(field).l2_norm() > 0.0_d);
}

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    using namespace boost::ut;

    "publishing a candidate advances the accepted epoch and retains the previous state in 2D and 3D"_test = [] {
        check_publication<2>();
        check_publication<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
