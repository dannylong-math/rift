#pragma once

#include "discrete_state_test_support.hpp"

#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <map>
#include <optional>
#include <rift/discrete_state.hpp>
#include <set>
#include <stdexcept>

namespace rift_test::state_store_model_00 {

enum class Scenario : std::uint8_t { publish, discard, stale_sibling, private_child };

struct StateModel {
    explicit StateModel(const std::uint64_t initial) : accepted(initial) {}

    std::uint64_t accepted;
    std::optional<std::uint64_t> previous;
    std::map<std::uint64_t, std::uint64_t> private_roots;
    std::set<std::uint64_t> retired;

    [[nodiscard]] std::uint64_t root_for(const std::uint64_t base) const
    {
        const auto found = private_roots.find(base);
        return found == private_roots.end() ? base : found->second;
    }

    void seal(const std::uint64_t snapshot, const std::uint64_t base)
    {
        private_roots.emplace(snapshot, root_for(base));
    }

    [[nodiscard]] bool publish(const std::uint64_t snapshot)
    {
        const auto found = private_roots.find(snapshot);
        if (found == private_roots.end() || found->second != accepted) {
            return false;
        }
        previous = accepted;
        accepted = snapshot;
        private_roots.erase(found);
        return true;
    }

    void discard(const std::uint64_t snapshot)
    {
        private_roots.erase(snapshot);
        retired.insert(snapshot);
    }
};

inline void expect_model(const rift::StateStore& store, const StateModel& model)
{
    using namespace boost::ut;
    expect(store.snapshot(rift::StateSlot::accepted).stamp().snapshot.value() == model.accepted);
    if (model.previous) {
        expect(store.snapshot(rift::StateSlot::previous).stamp().snapshot.value() == *model.previous);
    }
    else {
        expect(throws<std::out_of_range>([&] { static_cast<void>(store.snapshot(rift::StateSlot::previous)); }));
    }
    for (const auto& [snapshot, root] : model.private_roots) {
        static_cast<void>(root);
        expect(!store.snapshot(rift::StateSnapshotId::from_index(snapshot)).stamp().published_epoch.has_value());
    }
    for (const auto snapshot : model.retired) {
        expect(throws<std::out_of_range>(
            [&] { static_cast<void>(store.snapshot(rift::StateSnapshotId::from_index(snapshot))); }));
    }
}

template<int dim> void run_scenario(const Scenario scenario)
{
    using namespace boost::ut;
    const auto space = rift::test::make_space_with_one_phase_field<dim>();
    auto store = rift::test::make_state_store(space, rift::StateRetentionPolicy{.max_pinned_private_snapshots = 1});
    const auto initial = store.snapshot(rift::StateSlot::accepted).stamp().snapshot;
    StateModel model(initial.value());
    expect_model(store, model);

    auto first_trial = store.begin_trial_collective(initial).value();
    expect(first_trial.active());
    const auto first = first_trial.seal_collective().value();
    expect(!first_trial.active());
    model.seal(first.stamp().snapshot.value(), initial.value());
    expect_model(store, model);

    if (scenario == Scenario::publish) {
        expect(model.publish(first.stamp().snapshot.value()));
        static_cast<void>(store.publish_collective(first.stamp().snapshot).value());
        expect_model(store, model);
        return;
    }
    if (scenario == Scenario::discard) {
        model.discard(first.stamp().snapshot.value());
        expect(store.discard_collective(first.stamp().snapshot).has_value());
        expect_model(store, model);
        return;
    }

    const auto child_base = scenario == Scenario::private_child ? first.stamp().snapshot : initial;
    auto second_trial = store.begin_trial_collective(child_base).value();
    expect(store.pin_collective(first.stamp().snapshot).has_value());
    expect(second_trial.active());
    const auto second = second_trial.seal_collective().value();
    expect(!second_trial.active());
    model.seal(second.stamp().snapshot.value(), child_base.value());
    expect_model(store, model);

    expect(model.publish(first.stamp().snapshot.value()));
    static_cast<void>(store.publish_collective(first.stamp().snapshot).value());
    expect_model(store, model);
    const auto rejected = store.publish_collective(second.stamp().snapshot);
    expect(!rejected.has_value());
    expect(rejected.error().code == rift::StateTransitionErrorCode::stale_accepted_root);
    expect(!model.publish(second.stamp().snapshot.value()));
    expect_model(store, model);
    model.discard(second.stamp().snapshot.value());
    expect(store.discard_collective(second.stamp().snapshot).has_value());
    expect_model(store, model);
}

template<int dim> void check_short_sequence_model()
{
    for (const auto scenario :
         {Scenario::publish, Scenario::discard, Scenario::stale_sibling, Scenario::private_child}) {
        run_scenario<dim>(scenario);
    }
}

} // namespace rift_test::state_store_model_00

namespace rift_test::state_store_model_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "a deterministic model predicts multiple short collective state sequences in 2D and 3D"_test = [] {
        check_short_sequence_model<2>();
        check_short_sequence_model<3>();
    };
}

} // namespace rift_test::state_store_model_00
