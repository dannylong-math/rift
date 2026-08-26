#pragma once

#include "../state_result_test_support.hpp"
#include "state_collective_test_support.hpp"

#include <algorithm>
#include <boost/ut.hpp>
#include <deal.II/base/mpi.h>
#include <mpi.h>
#include <rift/discrete_state.hpp>

namespace rift_test::mpi::state_level_set_revision_00 {

template<class Id> void expect_rank_agreement(const Id id)
{
    using namespace boost::ut;
    const auto gathered = dealii::Utilities::MPI::all_gather(MPI_COMM_WORLD, id.value());
    expect(std::ranges::all_of(gathered, [&](const auto value) { return value == gathered.front(); }));
}

template<int dim> void check_nonroot_revision_agreement()
{
    using namespace boost::ut;
    const auto rank = dealii::Utilities::MPI::this_mpi_process(MPI_COMM_WORLD);
    const auto fixture = rift::test::make_distributed_state_fixture<dim>();
    auto store = rift::test::require_state_result(rift::make_state_store(fixture.space.layout(), {}));
    const auto accepted = store.snapshot(rift::StateSlot::accepted);
    const auto level_set =
        rift::test::require_state_result(fixture.space.layout().field_reference(fixture.space.level_set_space().id()));

    auto changed_trial = rift::test::require_state_result(store.begin_trial_collective(accepted.stamp().snapshot));
    auto& changed_values = rift::test::require_state_result(changed_trial.field(level_set)).get();
    if (rank == 1U) {
        expect(changed_values.locally_owned_size() > 0U);
        changed_values.local_element(0) = 4.25;
    }
    const auto changed = rift::test::require_state_result(changed_trial.seal_collective());
    expect(changed.level_set_snapshot() != accepted.level_set_snapshot());
    expect(changed.level_set_snapshot().value() == accepted.level_set_snapshot().value() + 1U);
    expect_rank_agreement(changed.level_set_snapshot());

    auto unchanged_trial = rift::test::require_state_result(store.begin_trial_collective(changed.stamp().snapshot));
    const auto unchanged = rift::test::require_state_result(unchanged_trial.seal_collective());
    expect(unchanged.level_set_snapshot() == changed.level_set_snapshot());
    expect_rank_agreement(unchanged.level_set_snapshot());
}

inline void register_tests()
{
    using namespace boost::ut;
    "a finite level-set edit on only the nonroot or middle rank advances one agreed revision"_test = [] {
        check_nonroot_revision_agreement<2>();
        check_nonroot_revision_agreement<3>();
    };
}

} // namespace rift_test::mpi::state_level_set_revision_00
