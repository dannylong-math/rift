#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <stdexcept>

template<int dim> void check_discard()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    rift::StateStore store(space.layout());
    const auto accepted = store.snapshot(rift::StateSlot::accepted);

    auto transaction = store.begin_trial(accepted.stamp().snapshot);
    const auto candidate = transaction.seal();
    store.discard(candidate.stamp().snapshot);

    bool candidate_gone = false;
    try {
        static_cast<void>(store.snapshot(candidate.stamp().snapshot));
    }
    catch (const std::out_of_range&) {
        candidate_gone = true;
    }

    bool accepted_protected = false;
    try {
        store.discard(accepted.stamp().snapshot);
    }
    catch (const std::logic_error&) {
        accepted_protected = true;
    }

    expect(candidate_gone);
    expect(accepted_protected);
}

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);
    using namespace boost::ut;

    "discard removes a private snapshot but cannot remove accepted state in 2D and 3D"_test = [] {
        check_discard<2>();
        check_discard<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
