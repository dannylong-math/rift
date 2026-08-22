#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>

template<int dim> void check_level_set_identity()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    rift::StateStore store(space.layout());
    const auto initial = store.snapshot(rift::StateSlot::accepted);

    auto field_trial = store.begin_trial(initial.stamp().snapshot);
    field_trial.field(space.field_spaces().front().id()) = 1.0;
    const auto field_candidate = field_trial.seal();
    expect(field_candidate.level_set_snapshot() == initial.level_set_snapshot());

    auto unchanged_level_set_trial = store.begin_trial(field_candidate.stamp().snapshot);
    unchanged_level_set_trial.field(space.level_set_space().id()) = 0.0;
    const auto unchanged_level_set = unchanged_level_set_trial.seal();
    expect(unchanged_level_set.level_set_snapshot() == initial.level_set_snapshot());

    auto level_set_trial = store.begin_trial(unchanged_level_set.stamp().snapshot);
    level_set_trial.field(space.level_set_space().id()) = 2.0;
    const auto changed_level_set = level_set_trial.seal();
    expect(changed_level_set.level_set_snapshot() != initial.level_set_snapshot());
}

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    using namespace boost::ut;

    "the level-set snapshot identity changes only with level-set values in 2D and 3D"_test = [] {
        check_level_set_identity<2>();
        check_level_set_identity<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
