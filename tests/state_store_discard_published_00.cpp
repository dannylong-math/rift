#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>
#include <stdexcept>

namespace {

template<int dim> void check_older_published_state_is_protected()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    rift::StateStore store(space.layout());
    const auto initial = store.snapshot(rift::StateSlot::accepted);

    auto first_trial = store.begin_trial(initial.stamp().snapshot);
    const auto first = store.publish(first_trial.seal().stamp().snapshot);
    auto second_trial = store.begin_trial(first.stamp().snapshot);
    static_cast<void>(store.publish(second_trial.seal().stamp().snapshot));

    bool rejected = false;
    try {
        store.discard(initial.stamp().snapshot);
    }
    catch (const std::logic_error&) {
        rejected = true;
    }

    expect(rejected);
    expect(store.snapshot(initial.stamp().snapshot).stamp().published_epoch.has_value());
}

} // namespace

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize const mpi(argc, argv, 1);
    using namespace boost::ut;

    "discard preserves older published revisions in 2D and 3D"_test = [] {
        check_older_published_state_is_protected<2>();
        check_older_published_state_is_protected<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
