#pragma once

#include "../state_result_test_support.hpp"
#include "state_collective_test_support.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <boost/ut.hpp>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <expected>
#include <limits>
#include <memory>
#include <mpi.h>
#include <mpi_proto.h>
#include <optional>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <utility>

namespace rift_test::mpi::state_regional_agreement_00 {

inline void expect_error(const rift::StateTransitionResult<void>& result, const rift::StateTransitionErrorCode code)
{
    using namespace boost::ut;
    expect(!result.has_value());
    expect(result.error().code == code);
}

inline rift::StateTransitionResult<void> as_void(rift::StateTransitionResult<rift::StateSnapshot> result)
{
    if (!result) {
        return std::unexpected(result.error());
    }
    return {};
}

class SequenceOverride {
public:
    explicit SequenceOverride(const std::array<std::uint64_t, 5>& replacement) :
        original_(rift::detail::StateStoreAccess::replace_identity_sequences_for_test(replacement))
    {
    }

    SequenceOverride(const SequenceOverride&) = delete;
    SequenceOverride& operator=(const SequenceOverride&) = delete;
    SequenceOverride(SequenceOverride&&) = delete;
    SequenceOverride& operator=(SequenceOverride&&) = delete;
    ~SequenceOverride()
    {
        static_cast<void>(rift::detail::StateStoreAccess::replace_identity_sequences_for_test(original_));
    }

private:
    std::array<std::uint64_t, 5> original_;
};

inline void expect_sequences(const std::array<std::uint64_t, 5>& expected)
{
    using namespace boost::ut;
    const auto observed = rift::detail::StateStoreAccess::replace_identity_sequences_for_test(expected);
    expect(observed == expected);
}

template<typename Operation>
void expect_error_without_identity_change(Operation&& operation, const rift::StateTransitionErrorCode code)
{
    constexpr auto exhausted = static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
    constexpr std::array<std::uint64_t, 5> sentinel{exhausted, exhausted, exhausted, exhausted, exhausted};
    const SequenceOverride override(sentinel);
    expect_error(std::forward<Operation>(operation)(), code);
    expect_sequences(sentinel);
}

inline void expect_transaction_storage(const rift::MutableStateTransaction& transaction, const unsigned int rank,
                                       const rift::RegionalEntryId entry, const std::uint64_t expected)
{
    using namespace boost::ut;
    expect(rift::detail::StateStoreAccess::regional_cache_bits(transaction, entry) == expected);
    const auto backend = rift::detail::StateStoreAccess::regional_backend_bits(transaction, entry);
    expect(backend.has_value() == (rank == 0U));
    if (backend) {
        expect(*backend == expected);
    }
}

template<int dim> void check_regional_collective_agreement()
{
    using namespace boost::ut;
    constexpr std::uint64_t finite_a = 0x3ff0000000000001ULL;
    constexpr std::uint64_t finite_b = 0x3ff0000000000002ULL;
    constexpr std::uint64_t negative_zero = 0x8000000000000000ULL;
    constexpr std::uint64_t nan_a = 0x7ff8000000000042ULL;
    constexpr std::uint64_t nan_b = 0x7ff8000000000043ULL;
    const auto rank = dealii::Utilities::MPI::this_mpi_process(MPI_COMM_WORLD);
    const auto size = dealii::Utilities::MPI::n_mpi_processes(MPI_COMM_WORLD);
    const auto outlier = size / 2U;
    const auto fixture = rift::test::make_distributed_state_fixture<dim>({{"regional_a"}, {"regional_b"}});
    const auto first = fixture.space.layout().regional_entries().front().id;
    const auto second = fixture.space.layout().regional_entries().back().id;

    auto store = rift::test::require_state_result(rift::make_state_store(fixture.space.layout(), {}));
    const auto base = store.snapshot(rift::StateSlot::accepted);
    auto entry_trial = rift::test::require_state_result(store.begin_trial_collective(base.stamp().snapshot));

    expect_error_without_identity_change(
        [&] {
            return entry_trial.set_regional_value_collective(rank == outlier ? second : first,
                                                             std::bit_cast<double>(finite_a));
        },
        rift::StateTransitionErrorCode::argument_mismatch);
    expect_transaction_storage(entry_trial, rank, first, 0U);
    expect_transaction_storage(entry_trial, rank, second, 0U);
    expect(entry_trial.set_regional_value_collective(second, std::bit_cast<double>(finite_a)).has_value());
    const auto entry_candidate = rift::test::require_state_result(entry_trial.seal_collective());
    expect(std::bit_cast<std::uint64_t>(entry_candidate.regional_value(first)) == std::uint64_t{0});
    expect(std::bit_cast<std::uint64_t>(entry_candidate.regional_value(second)) == finite_a);

    auto trial = rift::test::require_state_result(store.begin_trial_collective(base.stamp().snapshot));
    expect_error_without_identity_change(
        [&] {
            return trial.set_regional_value_collective(first,
                                                       std::bit_cast<double>(rank == outlier ? finite_b : finite_a));
        },
        rift::StateTransitionErrorCode::argument_mismatch);
    expect_transaction_storage(trial, rank, first, 0U);
    expect_error_without_identity_change(
        [&] {
            return trial.set_regional_value_collective(
                first, std::bit_cast<double>(rank == outlier ? negative_zero : std::uint64_t{0}));
        },
        rift::StateTransitionErrorCode::argument_mismatch);
    expect_transaction_storage(trial, rank, first, 0U);

    expect(trial.set_regional_value_collective(first, std::bit_cast<double>(nan_a)).has_value());
    expect_transaction_storage(trial, rank, first, nan_a);
    expect_error_without_identity_change(
        [&] {
            return trial.set_regional_value_collective(first, std::bit_cast<double>(rank == outlier ? nan_b : nan_a));
        },
        rift::StateTransitionErrorCode::argument_mismatch);
    expect_transaction_storage(trial, rank, first, nan_a);

    expect_error_without_identity_change(
        [&] {
            return rank == outlier ? as_void(trial.seal_collective())
                                   : trial.set_regional_value_collective(first, std::bit_cast<double>(finite_a));
        },
        rift::StateTransitionErrorCode::operation_mismatch);
    expect(trial.active());
    expect(rift::detail::StateStoreAccess::regional_cache_bits(trial, first) == nan_a);

    auto other_trial = rift::test::require_state_result(store.begin_trial_collective(base.stamp().snapshot));
    auto& selected_trial = rank == outlier ? other_trial : trial;
    expect_error_without_identity_change(
        [&] { return selected_trial.set_regional_value_collective(first, std::bit_cast<double>(finite_a)); },
        rift::StateTransitionErrorCode::transaction_mismatch);
    expect_transaction_storage(trial, rank, first, nan_a);
    expect_transaction_storage(other_trial, rank, first, 0U);

    auto other_store = rift::test::require_state_result(rift::make_state_store(fixture.space.layout(), {}));
    const auto other_base = other_store.snapshot(rift::StateSlot::accepted);
    auto other_store_trial =
        rift::test::require_state_result(other_store.begin_trial_collective(other_base.stamp().snapshot));
    auto& selected_store_trial = rank == outlier ? other_store_trial : trial;
    expect_error_without_identity_change(
        [&] { return selected_store_trial.set_regional_value_collective(first, std::bit_cast<double>(finite_a)); },
        rift::StateTransitionErrorCode::store_mismatch);
    expect_transaction_storage(trial, rank, first, nan_a);
    expect_transaction_storage(other_store_trial, rank, first, 0U);

    auto expiring_store = std::optional<rift::StateStore>{
        rift::test::require_state_result(rift::make_state_store(fixture.space.layout(), {}))};
    const auto expiring_base = expiring_store->snapshot(rift::StateSlot::accepted);
    auto expiring_trial =
        rift::test::require_state_result(expiring_store->begin_trial_collective(expiring_base.stamp().snapshot));
    if (rank == outlier) {
        expiring_store.reset();
    }
    expect_error_without_identity_change(
        [&] {
            return expiring_trial.set_regional_value_collective(rank == outlier ? second : first,
                                                                std::bit_cast<double>(finite_a));
        },
        rift::StateTransitionErrorCode::argument_mismatch);
    expiring_store.reset();
    expect_error_without_identity_change(
        [&] { return expiring_trial.set_regional_value_collective(first, std::bit_cast<double>(finite_a)); },
        rift::StateTransitionErrorCode::expired_store);

    auto inactive_store = rift::test::require_state_result(rift::make_state_store(fixture.space.layout(), {}));
    const auto inactive_base = inactive_store.snapshot(rift::StateSlot::accepted);
    auto inactive_trial =
        rift::test::require_state_result(inactive_store.begin_trial_collective(inactive_base.stamp().snapshot));
    if (rank == outlier) {
        inactive_trial.abandon();
    }
    expect_error_without_identity_change(
        [&] {
            return inactive_trial.set_regional_value_collective(
                first, std::bit_cast<double>(rank == outlier ? finite_b : finite_a));
        },
        rift::StateTransitionErrorCode::argument_mismatch);
    expect_error_without_identity_change(
        [&] { return inactive_trial.set_regional_value_collective(first, std::bit_cast<double>(finite_a)); },
        rift::StateTransitionErrorCode::inactive_transaction);
    expect_error_without_identity_change(
        [&] { return inactive_trial.set_regional_value_collective(rift::RegionalEntryId::from_index(100), 1.0); },
        rift::StateTransitionErrorCode::inactive_transaction);

    expect_error_without_identity_change(
        [&] { return other_trial.set_regional_value_collective(rift::RegionalEntryId::from_index(100), 1.0); },
        rift::StateTransitionErrorCode::unknown_regional_entry);

    const auto exhausted = static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
    const std::array<std::uint64_t, 5> sentinel{exhausted, exhausted, exhausted, exhausted, exhausted};
    {
        const SequenceOverride override(sentinel);
        expect_error(other_trial.set_regional_value_collective(rift::RegionalEntryId::from_index(100), 1.0),
                     rift::StateTransitionErrorCode::unknown_regional_entry);
        expect_sequences(sentinel);
        expect(other_trial.set_regional_value_collective(first, std::bit_cast<double>(finite_b)).has_value());
        expect_sequences(sentinel);
    }

    expect(trial.set_regional_value_collective(first, std::bit_cast<double>(finite_a)).has_value());
    const auto candidate = rift::test::require_state_result(trial.seal_collective());
    const auto gathered_bits = dealii::Utilities::MPI::all_gather(
        MPI_COMM_WORLD, std::bit_cast<std::uint64_t>(candidate.regional_value(first)));
    expect(std::ranges::all_of(gathered_bits, [&](const auto value) { return value == finite_a; }));
    expect(rift::detail::StateStoreAccess::regional_cache_bits(candidate, first) == finite_a);
    const auto backend = rift::detail::StateStoreAccess::regional_backend_bits(candidate, first);
    expect(backend.has_value() == (rank == 0U));
    if (backend) {
        expect(*backend == finite_a);
    }

    auto second_entry_trial = rift::test::require_state_result(store.begin_trial_collective(base.stamp().snapshot));
    expect(second_entry_trial.set_regional_value_collective(second, std::bit_cast<double>(finite_b)).has_value());
    expect_transaction_storage(second_entry_trial, rank, first, 0U);
    expect_transaction_storage(second_entry_trial, rank, second, finite_b);
    const auto second_entry_candidate = rift::test::require_state_result(second_entry_trial.seal_collective());
    expect(std::bit_cast<std::uint64_t>(second_entry_candidate.regional_value(first)) == std::uint64_t{0});
    expect(std::bit_cast<std::uint64_t>(second_entry_candidate.regional_value(second)) == finite_b);

    auto synchronization_trial = rift::test::require_state_result(store.begin_trial_collective(base.stamp().snapshot));
    rift::detail::StateStoreAccess::set_regional_backend_bits_for_test(synchronization_trial, second, nan_b);
    expect(rift::detail::StateStoreAccess::regional_cache_bits(synchronization_trial, second) == std::uint64_t{0});
    const auto synchronization_backend =
        rift::detail::StateStoreAccess::regional_backend_bits(synchronization_trial, second);
    expect(synchronization_backend.has_value() == (rank == 0U));
    if (synchronization_backend) {
        expect(*synchronization_backend == nan_b);
    }
    const auto synchronized_candidate = rift::test::require_state_result(synchronization_trial.seal_collective());
    const auto synchronized_bits = dealii::Utilities::MPI::all_gather(
        MPI_COMM_WORLD, std::bit_cast<std::uint64_t>(synchronized_candidate.regional_value(second)));
    expect(std::ranges::all_of(synchronized_bits, [&](const auto value) { return value == nan_b; }));

    const auto level_set =
        rift::test::require_state_result(fixture.space.layout().field_reference(fixture.space.level_set_space().id()));
    auto nonroot_trial = rift::test::require_state_result(store.begin_trial_collective(base.stamp().snapshot));
    auto& nonroot_values = rift::test::require_state_result(nonroot_trial.field(level_set)).get();
    if (rank == outlier) {
        nonroot_values.local_element(nonroot_values.locally_owned_size() - 1U) = std::bit_cast<double>(negative_zero);
    }
    const auto nonroot_candidate = rift::test::require_state_result(nonroot_trial.seal_collective());
    expect(nonroot_candidate.level_set_snapshot() != base.level_set_snapshot());

    auto two_change_trial = rift::test::require_state_result(store.begin_trial_collective(base.stamp().snapshot));
    auto& two_change_values = rift::test::require_state_result(two_change_trial.field(level_set)).get();
    if (rank != 0U || size == 2U) {
        two_change_values.local_element(two_change_values.locally_owned_size() - 1U) =
            std::bit_cast<double>(rank == outlier ? negative_zero : std::uint64_t{1});
    }
    const auto two_change_candidate = rift::test::require_state_result(two_change_trial.seal_collective());
    expect(two_change_candidate.level_set_snapshot() != base.level_set_snapshot());
}

template<int dim> void check_reversed_communicator_owner()
{
    using namespace boost::ut;
    constexpr std::uint64_t bits = 0xfff8000000000042ULL;
    const auto world_rank = dealii::Utilities::MPI::this_mpi_process(MPI_COMM_WORLD);
    const auto world_size = dealii::Utilities::MPI::n_mpi_processes(MPI_COMM_WORLD);
    MPI_Comm reversed = MPI_COMM_NULL;
    expect(MPI_Comm_split(MPI_COMM_WORLD, 0, static_cast<int>(world_size - 1U - world_rank), &reversed) == MPI_SUCCESS);
    {
        const auto reversed_rank = dealii::Utilities::MPI::this_mpi_process(reversed);
        auto run = rift::test::require_state_result(rift::RunConfiguration::create(reversed));
        auto graph = rift::test::require_state_result(rift::make_phase_graph(run, {{"gas", "compressible"}}, {}));
        const auto gas = rift::test::require_state_result(
            graph.reference(rift::test::require_state_result(graph.find_phase("gas"))));
        auto triangulation = std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(reversed);
        dealii::GridGenerator::subdivided_hyper_cube(*triangulation, 2);
        rift::SupportEnvelope owned;
        for (const auto& cell : triangulation->active_cell_iterators()) {
            if (cell->is_locally_owned()) {
                owned.insert(cell->id());
            }
        }
        std::unique_ptr<dealii::Triangulation<dim>> consumed = std::move(triangulation);
        auto mesh = rift::test::require_state_result(rift::make_mesh_snapshot(run, std::move(consumed)));
        auto space = rift::test::make_distributed_state_space(mesh, graph, gas, owned, "flow", {{"regional"}});
        const auto entry = space.layout().regional_entries().front().id;
        auto store = rift::test::require_state_result(rift::make_state_store(space.layout(), {}));
        const auto base = store.snapshot(rift::StateSlot::accepted);
        auto trial = rift::test::require_state_result(store.begin_trial_collective(base.stamp().snapshot));
        expect(trial.set_regional_value_collective(entry, std::bit_cast<double>(bits)).has_value());
        expect(rift::detail::StateStoreAccess::regional_cache_bits(trial, entry) == bits);
        const auto trial_backend = rift::detail::StateStoreAccess::regional_backend_bits(trial, entry);
        expect(trial_backend.has_value() == (reversed_rank == 0U));
        expect(trial_backend.has_value() == (world_rank == world_size - 1U));
        const auto candidate = rift::test::require_state_result(trial.seal_collective());
        expect(std::bit_cast<std::uint64_t>(candidate.regional_value(entry)) == bits);
        const auto snapshot_backend = rift::detail::StateStoreAccess::regional_backend_bits(candidate, entry);
        expect(snapshot_backend.has_value() == (reversed_rank == 0U));
        if (snapshot_backend) {
            expect(*snapshot_backend == bits);
        }
    }
    expect(MPI_Comm_free(&reversed) == MPI_SUCCESS);
}

inline void register_tests()
{
    using namespace boost::ut;
    "regional state agrees exactly and preserves owner-independent caches"_test = [] {
        check_regional_collective_agreement<2>();
        check_regional_collective_agreement<3>();
        check_reversed_communicator_owner<2>();
        check_reversed_communicator_owner<3>();
    };
}

} // namespace rift_test::mpi::state_regional_agreement_00
