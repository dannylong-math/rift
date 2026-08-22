#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <stdexcept>

template<int dim> void check_trial_isolation()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    rift::StateStore store(space.layout());
    const auto base = store.snapshot(rift::StateSlot::accepted);
    const auto field = space.field_spaces().front().id();

    auto transaction = store.begin_trial(base.stamp().snapshot);
    transaction.field(field) = 3.0;
    const auto candidate = transaction.seal();

    expect(base.field(field).l2_norm() == 0.0_d);
    expect(candidate.field(field).l2_norm() > 0.0_d);
    expect(candidate.stamp().snapshot != base.stamp().snapshot);
    expect(!candidate.stamp().published_epoch.has_value());
    expect(!transaction.active());

    bool rejected_mutable_alias = false;
    try {
        static_cast<void>(transaction.field(field));
    }
    catch (const std::logic_error&) {
        rejected_mutable_alias = true;
    }
    expect(rejected_mutable_alias);
}

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    using namespace boost::ut;

    "sealing a trial leaves its immutable base unchanged in 2D and 3D"_test = [] {
        check_trial_isolation<2>();
        check_trial_isolation<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
