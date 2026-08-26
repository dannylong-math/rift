#pragma once

#include "discrete_state_test_support.hpp"

#include <array>
#include <boost/ut.hpp>
#include <cstddef>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <limits>
#include <optional>
#include <rift/discrete_state.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rift_test::state_store_retention_model_00 {

struct RetentionModel {
    explicit RetentionModel(const std::size_t capacity_value) : capacity(capacity_value) {}

    std::size_t capacity;
    std::size_t accepted{};
    std::optional<std::size_t> previous;
    std::optional<std::size_t> transient;
    std::set<std::size_t> pinned;
    std::vector<std::size_t> roots{0};

    [[nodiscard]] bool known(const std::size_t token) const
    {
        return token == accepted || previous == token || transient == token || pinned.contains(token);
    }

    [[nodiscard]] bool private_candidate(const std::size_t token) const
    {
        return transient == token || pinned.contains(token);
    }

    std::size_t seal(const std::size_t base)
    {
        const auto token = roots.size();
        roots.push_back(private_candidate(base) ? roots.at(base) : base);
        transient = token;
        return token;
    }

    [[nodiscard]] std::optional<rift::StateTransitionErrorCode> pin(const std::size_t token)
    {
        if (!known(token)) {
            return rift::StateTransitionErrorCode::unknown_snapshot;
        }
        if (!private_candidate(token)) {
            return rift::StateTransitionErrorCode::wrong_candidate_state;
        }
        if (pinned.contains(token)) {
            return std::nullopt;
        }
        if (pinned.size() >= capacity) {
            return rift::StateTransitionErrorCode::pin_limit_reached;
        }
        pinned.insert(token);
        transient.reset();
        return std::nullopt;
    }

    [[nodiscard]] std::optional<rift::StateTransitionErrorCode> unpin(const std::size_t token)
    {
        if (!known(token)) {
            return rift::StateTransitionErrorCode::unknown_snapshot;
        }
        if (!private_candidate(token)) {
            return rift::StateTransitionErrorCode::wrong_candidate_state;
        }
        if (!pinned.contains(token)) {
            return std::nullopt;
        }
        pinned.erase(token);
        transient = token;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<rift::StateTransitionErrorCode> publish(const std::size_t token)
    {
        if (!known(token)) {
            return rift::StateTransitionErrorCode::unknown_snapshot;
        }
        if (!private_candidate(token)) {
            return rift::StateTransitionErrorCode::wrong_candidate_state;
        }
        if (roots.at(token) != accepted) {
            return rift::StateTransitionErrorCode::stale_accepted_root;
        }
        pinned.erase(token);
        if (transient == token) {
            transient.reset();
        }
        previous = accepted;
        accepted = token;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<rift::StateTransitionErrorCode> discard(const std::size_t token)
    {
        if (!known(token)) {
            return rift::StateTransitionErrorCode::unknown_snapshot;
        }
        if (transient != token) {
            return rift::StateTransitionErrorCode::invalid_discard;
        }
        transient.reset();
        return std::nullopt;
    }
};

enum class ModelAction : std::uint8_t { seal, pin, unpin, publish, discard, begin };

inline std::string_view action_name(const ModelAction action)
{
    switch (action) {
    case ModelAction::seal:
        return "seal";
    case ModelAction::pin:
        return "pin";
    case ModelAction::unpin:
        return "unpin";
    case ModelAction::publish:
        return "publish";
    case ModelAction::discard:
        return "discard";
    case ModelAction::begin:
        return "begin";
    }
    std::unreachable();
}

template<int dim> class ModelRunner {
public:
    explicit ModelRunner(const std::size_t capacity) :
        space_(rift::test::make_space_with_one_phase_field<dim>()),
        store_(
            rift::test::make_state_store(space_, rift::StateRetentionPolicy{.max_pinned_private_snapshots = capacity})),
        model_(capacity),
        ids_{store_.snapshot(rift::StateSlot::accepted).stamp().snapshot},
        trace_("capacity=" + std::to_string(capacity))
    {
        check_lookup();
    }

    std::size_t seal(const std::size_t base)
    {
        using namespace boost::ut;
        const auto token = model_.seal(base);
        auto trial = store_.begin_trial_collective(ids_.at(base)).value();
        const auto snapshot = trial.seal_collective().value();
        ids_.push_back(snapshot.stamp().snapshot);
        expect(token + 1U == ids_.size()) << trace_;
        check_lookup();
        return token;
    }

    void pin(const std::size_t token)
    {
        const auto expected = model_.pin(token);
        compare(store_.pin_collective(ids_.at(token)), expected);
        check_lookup();
    }

    void unpin(const std::size_t token)
    {
        const auto expected = model_.unpin(token);
        compare(store_.unpin_collective(ids_.at(token)), expected);
        check_lookup();
    }

    void publish(const std::size_t token)
    {
        const auto expected = model_.publish(token);
        const auto actual = store_.publish_collective(ids_.at(token));
        compare_result(actual, expected);
        check_lookup();
    }

    void discard(const std::size_t token)
    {
        const auto expected = model_.discard(token);
        compare(store_.discard_collective(ids_.at(token)), expected);
        check_lookup();
    }

    void begin(const std::size_t token)
    {
        using namespace boost::ut;
        auto actual = store_.begin_trial_collective(ids_.at(token));
        expect(actual.has_value() == model_.known(token)) << trace_;
        if (actual) {
            actual->abandon();
        }
        else {
            expect(actual.error().code == rift::StateTransitionErrorCode::unknown_snapshot) << trace_;
        }
        check_lookup();
    }

    void apply(const ModelAction action)
    {
        trace_ += " -> ";
        trace_ += action_name(action);
        const auto latest = ids_.size() - 1U;
        switch (action) {
        case ModelAction::seal:
            static_cast<void>(seal(model_.accepted));
            return;
        case ModelAction::pin:
            pin(latest);
            return;
        case ModelAction::unpin:
            unpin(latest);
            return;
        case ModelAction::publish:
            publish(latest);
            return;
        case ModelAction::discard:
            discard(latest);
            return;
        case ModelAction::begin:
            begin(latest);
            return;
        }
        std::unreachable();
    }

private:
    void compare(const rift::StateTransitionResult<void>& actual,
                 const std::optional<rift::StateTransitionErrorCode> expected) const
    {
        using namespace boost::ut;
        expect(actual.has_value() == !expected.has_value()) << trace_;
        if (expected) {
            expect(actual.error().code == *expected) << trace_;
        }
    }

    void compare_result(const rift::StateTransitionResult<rift::StateSnapshot>& actual,
                        const std::optional<rift::StateTransitionErrorCode> expected) const
    {
        using namespace boost::ut;
        expect(actual.has_value() == !expected.has_value()) << trace_;
        if (expected) {
            expect(actual.error().code == *expected) << trace_;
        }
    }

    void check_lookup() const
    {
        using namespace boost::ut;
        expect(store_.snapshot(rift::StateSlot::accepted).stamp().snapshot == ids_.at(model_.accepted)) << trace_;
        for (std::size_t token = 0; token < ids_.size(); ++token) {
            const bool lookup_succeeded = [&] {
                try {
                    static_cast<void>(store_.snapshot(ids_.at(token)));
                    return true;
                }
                catch (const std::out_of_range&) {
                    return false;
                }
            }();
            expect(lookup_succeeded == model_.known(token)) << trace_;
        }
    }

    rift::SpaceSnapshot<dim> space_;
    rift::StateStore store_;
    RetentionModel model_;
    std::vector<rift::StateSnapshotId> ids_;
    std::string trace_;
};

template<int dim> void check_all_short_model_sequences()
{
    constexpr std::array actions{ModelAction::seal,    ModelAction::pin,     ModelAction::unpin,
                                 ModelAction::publish, ModelAction::discard, ModelAction::begin};
    constexpr std::array capacities{std::size_t{0}, std::size_t{1}, std::size_t{2},
                                    std::numeric_limits<std::size_t>::max()};
    for (const auto capacity : capacities) {
        for (const auto second : actions) {
            for (const auto third : actions) {
                ModelRunner<dim> run(capacity);
                run.apply(ModelAction::seal);
                run.apply(second);
                run.apply(third);
            }
        }
    }
}

} // namespace rift_test::state_store_retention_model_00

namespace rift_test::state_store_retention_model_00 {

inline void register_tests()
{
    using namespace boost::ut;
    "an independent public lookup model enumerates every bounded short action sequence"_test = [] {
        check_all_short_model_sequences<2>();
        check_all_short_model_sequences<3>();
    };
}

} // namespace rift_test::state_store_retention_model_00
