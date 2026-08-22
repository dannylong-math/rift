#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <rift/discrete_state.hpp>
#include <utility>

namespace {

template<int dim> void check_transaction_moves()
{
    using namespace boost::ut;

    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    rift::StateStore store(space.layout());
    const auto accepted = store.snapshot(rift::StateSlot::accepted);
    const auto field = space.field_spaces().front().id();

    auto original = store.begin_trial(accepted.stamp().snapshot);
    original.field(field) = 6.0;
    auto moved = std::move(original);
    // The moved-from transaction has a documented observable inactive state.
    expect(!original.active()); // NOLINT(bugprone-use-after-move)
    expect(moved.active());

    auto destination = store.begin_trial(accepted.stamp().snapshot);
    destination = std::move(moved);
    // Move assignment gives the source the same documented inactive state.
    expect(!moved.active()); // NOLINT(bugprone-use-after-move)
    expect(destination.active());

    const auto candidate = destination.seal();
    expect(candidate.field(field).l2_norm() > 0.0_d);
}

} // namespace

int main(int argc, char** argv)
{
    dealii::Utilities::MPI::MPI_InitFinalize const mpi(argc, argv, 1);
    using namespace boost::ut;

    "moving a transaction transfers its sole mutable authority in 2D and 3D"_test = [] {
        check_transaction_moves<2>();
        check_transaction_moves<3>();
    };

    return static_cast<int>(boost::ut::cfg<>.run());
}
