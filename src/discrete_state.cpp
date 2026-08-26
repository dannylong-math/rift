/**
 * \file
 * \brief Collective state construction, immutable snapshots, and trial transactions.
 */

#include "run_configuration_internal.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <deal.II/base/types.h>
#include <exception>
#include <expected>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mpi.h>
#include <mpi_proto.h>
#include <mutex>
#include <new>
#include <optional>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace rift {

namespace detail {

void abort_space_operation(const std::shared_ptr<const RunConfigurationControl>& control, const int status)
{
    control->abort_handler()(control->communicator(), status);
    std::terminate();
}

namespace {

/**
 * \brief Own every distributed field and private regional backend vector in one revision.
 *
 * \code{.cpp}
 * std::size_t vector_count(
 *     const rift::detail::StateVectorStorage& storage) {
 *     return storage.fields.size() + storage.regional_entries.size();
 * }
 * \endcode
 *
 * Public field access is provenance checked. Each regional scalar keeps its
 * rank-zero backend for vector-bundle ownership and an exact replicated cache
 * for owner-independent local reads.
 */
struct StateVectorStorage {
    /** \brief Field vectors indexed by `FieldGroupId`. */
    std::vector<DistributedStateVector> fields;
    /** \brief Private regional backend vectors indexed by `RegionalEntryId`. */
    std::vector<DistributedStateVector> regional_entries;
    /** \brief Replicated exact binary64 representations indexed by regional identity. */
    std::vector<std::uint64_t> regional_cache;
};

} // namespace

/**
 * \brief Retain immutable collective identity and lifetime for all state handles.
 *
 * \code{.cpp}
 * bool retains_collective_lifetimes(
 *     const rift::detail::StateCollectiveContext& context) {
 *     return context.run_control != nullptr && context.mesh_lifetime != nullptr;
 * }
 * \endcode
 */
struct StateCollectiveContext {
    /** \brief Retained run communicator and fatal handler. */
    std::shared_ptr<const RunConfigurationControl> run_control;
    /** \brief Type-erased immutable mesh owner. */
    std::shared_ptr<const void> mesh_lifetime;
    /** \brief Complete finite-space provenance. */
    SpaceProvenance provenance;
    /** \brief Authoritative mesh communicator. */
    MPI_Comm communicator;
    /** \brief Communicator-consistent publication authority identity. */
    StateStoreId store;
    /** \brief Collectively agreed retention policy. */
    StateRetentionPolicy retention;
    /** \brief Checked field references indexed by group identity. */
    std::vector<StateFieldReference> fields;
    /** \brief Full-background level-set group. */
    FieldGroupId level_set_group;
};

/**
 * \brief Pair immutable vectors with complete cache and lineage identity.
 *
 * \code{.cpp}
 * rift::StateSnapshotId snapshot_id(
 *     const rift::detail::StateSnapshotData& data) {
 *     return data.stamp.snapshot;
 * }
 * \endcode
 */
struct StateSnapshotData {
    /** \brief Collective context retained by this immutable handle. */
    std::shared_ptr<const StateCollectiveContext> context;
    /** \brief Complete public snapshot stamp. */
    StateSnapshotStamp stamp;
    /** \brief Revision of the geometry field. */
    LevelSetFieldSetSnapshotId level_set_snapshot;
    /** \brief Accepted snapshot from which a private lineage descends. */
    StateSnapshotId accepted_root;
    /** \brief Shared immutable vectors. */
    std::shared_ptr<const StateVectorStorage> storage;
};

/**
 * \brief Keep a strong immutable base beside one mutable vector copy.
 *
 * \code{.cpp}
 * bool has_mutable_storage(
 *     const rift::detail::MutableStateData& data) {
 *     return data.base != nullptr && data.storage != nullptr;
 * }
 * \endcode
 */
struct MutableStateData {
    /** \brief Strong base lifetime used for isolation and level-set comparison. */
    std::shared_ptr<const StateSnapshotData> base;
    /** \brief Accepted lineage root propagated to the sealed candidate. */
    StateSnapshotId accepted_root;
    /** \brief Transaction-private mutable vectors. */
    std::unique_ptr<StateVectorStorage> storage;
};

/**
 * \brief Retain collective transaction identity after mutable authority ends.
 *
 * \code{.cpp}
 * rift::StateTransactionId transaction_id(
 *     const rift::detail::StateTransactionTombstone& tombstone) {
 *     return tombstone.transaction;
 * }
 * \endcode
 */
struct StateTransactionTombstone {
    /** \brief Immutable communicator/store context. */
    std::shared_ptr<const StateCollectiveContext> context;
    /** \brief Weak mutable store authority. */
    std::weak_ptr<StateStoreAuthority> authority;
    /** \brief Communicator-consistent trial identity. */
    StateTransactionId transaction;
    /** \brief Snapshot argument used to open this trial. */
    StateSnapshotId base;
};

/**
 * \brief Own the externally serialized mutable publication registry.
 *
 * \code{.cpp}
 * rift::StateSnapshotId accepted_id(
 *     const rift::detail::StateStoreAuthority& authority) {
 *     return authority.accepted->stamp.snapshot;
 * }
 * \endcode
 */
struct StateStoreAuthority {
    /**
     * \brief Pair one retained lookup record with its explicit pin state.
     *
     * \code{.cpp}
     * bool is_pinned_record(
     *     std::shared_ptr<const rift::detail::StateSnapshotData> snapshot) {
     *     const rift::detail::StateStoreAuthority::SnapshotRecord record{
     *         .snapshot = std::move(snapshot), .pinned = true};
     *     return record.pinned;
     * }
     * \endcode
     *
     * \par Maintainer workflow
     * Create the record when the snapshot first enters the already allocated
     * lookup node. Pin and unpin then toggle the flag without allocating.
     */
    struct SnapshotRecord {
        /** \brief Immutable snapshot data returned by lookup. */
        std::shared_ptr<const StateSnapshotData> snapshot;
        /** \brief Whether this private record consumes explicit pin capacity. */
        bool pinned{};
    };

    /** \brief Immutable collective context. */
    std::shared_ptr<const StateCollectiveContext> context;
    /** \brief Registered accepted, previous, and private snapshots. */
    std::map<std::uint64_t, SnapshotRecord> snapshots;
    /** \brief Current accepted state. */
    std::shared_ptr<const StateSnapshotData> accepted;
    /** \brief Immediately previous accepted state, when present. */
    std::shared_ptr<const StateSnapshotData> previous;
    /** \brief Sole unpinned private snapshot identity, when one is retained. */
    std::optional<std::uint64_t> transient;
    /** \brief Number of private records whose `pinned` flag is set. */
    std::size_t pinned_count{};
};

StateTransitionResult<std::array<std::uint64_t, 5>>
reserve_state_sequence_values(std::array<std::uint64_t, 5>& next, const StateSequenceReservation request)
{
    for (std::size_t index = 0; index < next.size(); ++index) {
        if ((request.mask & (1ULL << index)) != 0U && next.at(index) >= std::numeric_limits<std::uint32_t>::max()) {
            return std::unexpected(StateTransitionError{StateTransitionErrorCode::identity_exhausted});
        }
    }
    std::array<std::uint64_t, 5> values{};
    for (std::size_t index = 0; index < next.size(); ++index) {
        if ((request.mask & (1ULL << index)) != 0U) {
            values.at(index) = (static_cast<std::uint64_t>(request.origin_world_rank) << 32U) | next.at(index);
            ++next.at(index);
        }
    }
    return values;
}

void require_state_mpi_success(const std::shared_ptr<const RunConfigurationControl>& control,
                               const StateMpiStatus status)
{
    if (status.operation != MPI_SUCCESS) {
        abort_space_operation(control, status.fatal);
    }
}

} // namespace detail

namespace {

/**
 * \brief Name one operation in the common first collective envelope.
 *
 * Maintainers append values so the wire-level operation discriminator remains
 * stable when a transition is added. `agree_transition` compares this value
 * before every store, argument, state, or capacity field.
 */
enum class StateOperation : std::uint8_t {
    /** \brief Begin an isolated mutable transaction from a retained base. */
    begin = 1,
    /** \brief Seal an active mutable transaction into a private snapshot. */
    seal = 2,
    /** \brief Publish a current-root private candidate as accepted state. */
    publish = 3,
    /** \brief Remove the sole unpinned transient private candidate. */
    discard = 4,
    /** \brief Convert the transient private candidate into an explicit pin. */
    pin = 5,
    /** \brief Convert an explicit pin into the sole transient candidate. */
    unpin = 6,
    /** \brief Replace one transaction-private regional scalar exactly. */
    set_regional_value = 7
};

static_assert(sizeof(double) == sizeof(std::uint64_t));
static_assert(std::numeric_limits<double>::is_iec559);

/** \brief Select finite identity components reserved in one atomic bundle. */
inline constexpr std::uint64_t store_mask = detail::state_store_identity_mask;
/** \brief Select a transaction identity in the local reservation helper. */
inline constexpr std::uint64_t transaction_mask = detail::state_transaction_identity_mask;
/** \brief Select a snapshot identity in the local reservation helper. */
inline constexpr std::uint64_t snapshot_mask = detail::state_snapshot_identity_mask;
/** \brief Select an accepted epoch in the local reservation helper. */
inline constexpr std::uint64_t epoch_mask = detail::state_epoch_identity_mask;
/** \brief Select a level-set revision in the local reservation helper. */
inline constexpr std::uint64_t level_set_mask = detail::state_level_set_identity_mask;

/**
 * \brief Hold raw values from one communicator-consistent reservation.
 * \code{.cpp}
 * std::uint64_t published_value(const ReservedStateIdentities& ids) {
 *     return ids.snapshot;
 * }
 * \endcode
 */
struct ReservedStateIdentities {
    /** \brief Reserved store component, or zero when not selected. */
    std::uint64_t store{};
    /** \brief Reserved transaction component, or zero when not selected. */
    std::uint64_t transaction{};
    /** \brief Reserved snapshot component, or zero when not selected. */
    std::uint64_t snapshot{};
    /** \brief Reserved publication-epoch component, or zero when not selected. */
    std::uint64_t epoch{};
    /** \brief Reserved level-set revision component, or zero when not selected. */
    std::uint64_t level_set{};
};

/**
 * \brief Store root-local process-global sequences for every identity domain.
 * \code{.cpp}
 * std::uint64_t next_store(const StateIdentitySequences& sequences) {
 *     return sequences.next.front();
 * }
 * \endcode
 */
struct StateIdentitySequences {
    /** \brief Serialize every process-local reservation and test replacement. */
    std::mutex mutex;
    /** \brief Next values in store/transaction/snapshot/epoch/level-set order. */
    std::array<std::uint64_t, 5> next{};
};

/**
 * \brief Return process-global root sequences shared by every state communicator.
 * \return stable process-lifetime sequence registry; callers must lock `mutex`.
 * \par Maintainer workflow
 * Access only on communicator rank zero during reservation, or from the
 * externally serialized single-rank replacement seam.
 */
StateIdentitySequences& state_identity_sequences()
{
    static StateIdentitySequences sequences;
    return sequences;
}

/**
 * \brief Route an unsafe state-stage exception to the retained fatal handler.
 * \param control retained run fatal authority.
 * \param status deterministic MPI-compatible failure status.
 * \par Fatal behavior
 * This function does not return, even if an injected handler unexpectedly does.
 */
[[noreturn]] void abort_state(const std::shared_ptr<const detail::RunConfigurationControl>& control, const int status)
{
    detail::abort_space_operation(control, status);
}

/**
 * \brief Reserve selected state identity components without partial advancement.
 * \param control retained fatal authority for MPI failures.
 * \param communicator collective state communicator.
 * \param mesh identity whose origin world rank scopes every value.
 * \param mask selected store/transaction/snapshot/epoch/level-set components.
 * \return communicator-consistent values, or `identity_exhausted` with no advancement.
 * \par Maintainer workflow
 * Validate every logical descriptor before calling. Rank zero reserves beneath
 * the process-global lock and broadcasts the all-or-none result.
 */
StateTransitionResult<ReservedStateIdentities>
reserve_state_identities(const std::shared_ptr<const detail::RunConfigurationControl>& control,
                         const MPI_Comm communicator, const MeshSnapshotId mesh, const std::uint64_t mask)
{
    int rank = 0;
    detail::require_state_mpi_success(control,
                                      {.operation = MPI_Comm_rank(communicator, &rank), .fatal = MPI_ERR_OTHER});
    std::array<std::uint64_t, 6> payload{};
    if (rank == 0) {
        auto& sequences = state_identity_sequences();
        const std::scoped_lock lock(sequences.mutex);
        const auto origin = static_cast<std::uint32_t>(mesh.value() >> 32U);
        const auto values =
            detail::reserve_state_sequence_values(sequences.next, {.origin_world_rank = origin, .mask = mask});
        payload.at(0) = values ? 0U : 1U;
        if (values) {
            std::ranges::copy(*values, std::next(payload.begin()));
        }
    }
    detail::require_state_mpi_success(control, {.operation = MPI_Bcast(payload.data(), static_cast<int>(payload.size()),
                                                                       MPI_UINT64_T, 0, communicator),
                                                .fatal = MPI_ERR_OTHER});
    if (payload.front() != 0U) {
        return std::unexpected(StateTransitionError{StateTransitionErrorCode::identity_exhausted});
    }
    return ReservedStateIdentities{.store = payload.at(1),
                                   .transaction = payload.at(2),
                                   .snapshot = payload.at(3),
                                   .epoch = payload.at(4),
                                   .level_set = payload.at(5)};
}

/**
 * \brief Serialize complete replicated layout semantics without owner-local partitions.
 * \param layout replicated immutable layout.
 * \return length-framed canonical bytes used only for exact collective agreement.
 * \par Maintainer workflow
 * Add every new replicated layout field here, but never owner-local IndexSets.
 */
std::string canonical_layout(const StateLayout& layout)
{
    const auto provenance = layout.provenance();
    std::vector<std::string> values{
        std::to_string(provenance.run.value()),       std::to_string(provenance.graph.value()),
        std::to_string(provenance.mesh.value()),      std::to_string(provenance.registry.value()),
        std::to_string(provenance.epoch.value()),     std::to_string(layout.level_set_group().value()),
        std::to_string(layout.field_blocks().size()), std::to_string(layout.regional_entries().size()),
    };
    for (const auto& block : layout.field_blocks()) {
        values.emplace_back(std::to_string(block.id.value()));
        values.emplace_back(block.name);
        values.emplace_back(block.is_level_set ? "1" : "0");
        values.emplace_back(std::to_string(block.locally_owned_dofs.size()));
        if (block.phase) {
            values.emplace_back("1");
            values.emplace_back(std::to_string(block.phase->graph.run.value()));
            values.emplace_back(std::to_string(block.phase->graph.graph.value()));
            values.emplace_back(std::to_string(block.phase->phase.value()));
        }
        else {
            values.emplace_back("0");
        }
    }
    for (const auto& entry : layout.regional_entries()) {
        values.emplace_back(std::to_string(entry.id.value()));
        values.emplace_back(entry.name);
        values.emplace_back(std::to_string(entry.locally_owned_entries.size()));
    }
    return detail::pack_space_strings(values);
}

/**
 * \brief Return whether every rank supplied the exact same byte payload.
 * \param control retained fatal authority for the gather.
 * \param communicator communicator on which every rank participates.
 * \param local rank-local canonical byte payload.
 * \return true exactly when all gathered payloads match rank zero byte-for-byte.
 * \par Fatal behavior
 * MPI or gather-allocation failure routes through the retained fatal policy.
 */
bool collectively_equal(const std::shared_ptr<const detail::RunConfigurationControl>& control,
                        const MPI_Comm communicator, const std::string_view local)
{
    const auto gathered = detail::all_gather_space_payloads(control, communicator, local);
    return std::ranges::all_of(gathered, [&](const auto& value) { return value == gathered.front(); });
}

/**
 * \brief Describe the first fixed collective state-transition operation.
 * \code{.cpp}
 * bool is_active(const TransitionEnvelope& envelope) {
 *     return envelope.active != 0;
 * }
 * \endcode
 *
 * Keep this trivially copyable because agreement gathers its exact bytes.
 */
struct TransitionEnvelope {
    /** \brief `StateOperation` encoded as a fixed integer. */
    std::uint64_t operation{};
    /** \brief Store authority identity. */
    std::uint64_t store{};
    /** \brief Transaction or snapshot argument identity. */
    std::uint64_t subject{};
    /** \brief Secondary regional-entry argument, when the operation supplies one. */
    std::uint64_t entry{};
    /** \brief Exact binary64 value argument, when the operation supplies one. */
    std::uint64_t value_bits{};
    /** \brief Whether weak store authority is still live. */
    std::uint64_t authority{};
    /** \brief Whether a transaction still owns mutable state. */
    std::uint64_t active{};
    /** \brief Whether a snapshot argument is registered. */
    std::uint64_t known{};
    /** \brief Whether a registered snapshot is private. */
    std::uint64_t candidate{};
    /** \brief Whether a private snapshot is the unpinned transient. */
    std::uint64_t transient{};
    /** \brief Whether a private snapshot is explicitly pinned. */
    std::uint64_t pinned{};
    /** \brief Whether one additional pin fits the configured capacity. */
    std::uint64_t pin_available{};
    /** \brief Whether a candidate descends from current accepted state. */
    std::uint64_t root_current{};
};

/**
 * \brief Check post-subject regional, lifetime, lookup, and candidate precedence.
 * \param gathered fixed envelopes from every retained communicator rank.
 * \param local calling rank's envelope, used to select operation-specific checks.
 * \return success or the first approved post-subject logical error.
 */
StateTransitionResult<void> agree_transition_state(const std::span<const TransitionEnvelope> gathered,
                                                   const TransitionEnvelope& local)
{
    const auto differs = [&](auto member) {
        return std::ranges::any_of(gathered,
                                   [&](const auto& value) { return value.*member != gathered.front().*member; });
    };
    if (local.operation == static_cast<std::uint64_t>(StateOperation::set_regional_value) &&
        (differs(&TransitionEnvelope::entry) || differs(&TransitionEnvelope::value_bits))) {
        return std::unexpected(StateTransitionError{StateTransitionErrorCode::argument_mismatch});
    }
    if (std::ranges::any_of(gathered, [](const auto& value) { return value.authority == 0U; })) {
        return std::unexpected(StateTransitionError{StateTransitionErrorCode::expired_store});
    }
    if ((local.operation == static_cast<std::uint64_t>(StateOperation::seal) ||
         local.operation == static_cast<std::uint64_t>(StateOperation::set_regional_value)) &&
        std::ranges::any_of(gathered, [](const auto& value) { return value.active == 0U; })) {
        return std::unexpected(StateTransitionError{StateTransitionErrorCode::inactive_transaction});
    }
    if (local.operation == static_cast<std::uint64_t>(StateOperation::set_regional_value) &&
        std::ranges::any_of(gathered, [](const auto& value) { return value.known == 0U; })) {
        return std::unexpected(StateTransitionError{StateTransitionErrorCode::unknown_regional_entry});
    }
    if (local.operation != static_cast<std::uint64_t>(StateOperation::set_regional_value) &&
        std::ranges::any_of(gathered, [](const auto& value) { return value.known == 0U; })) {
        return std::unexpected(StateTransitionError{StateTransitionErrorCode::unknown_snapshot});
    }
    if ((local.operation == static_cast<std::uint64_t>(StateOperation::publish) ||
         local.operation == static_cast<std::uint64_t>(StateOperation::pin) ||
         local.operation == static_cast<std::uint64_t>(StateOperation::unpin)) &&
        std::ranges::any_of(gathered, [](const auto& value) { return value.candidate == 0U; })) {
        return std::unexpected(StateTransitionError{StateTransitionErrorCode::wrong_candidate_state});
    }
    if (local.operation == static_cast<std::uint64_t>(StateOperation::discard) &&
        std::ranges::any_of(gathered, [](const auto& value) { return value.transient == 0U; })) {
        return std::unexpected(StateTransitionError{StateTransitionErrorCode::invalid_discard});
    }
    if (local.operation == static_cast<std::uint64_t>(StateOperation::pin) &&
        std::ranges::any_of(gathered,
                            [](const auto& value) { return value.pinned == 0U && value.pin_available == 0U; })) {
        return std::unexpected(StateTransitionError{StateTransitionErrorCode::pin_limit_reached});
    }
    if (local.operation == static_cast<std::uint64_t>(StateOperation::publish) &&
        std::ranges::any_of(gathered, [](const auto& value) { return value.root_current == 0U; })) {
        return std::unexpected(StateTransitionError{StateTransitionErrorCode::stale_accepted_root});
    }
    return {};
}

/**
 * \brief Gather fixed transition descriptors and check deterministic precedence.
 * \param context immutable communicator/store context.
 * \param local fixed descriptor supplied by this rank.
 * \return success, or the first fixed-size logical error in approved precedence.
 * \par Fatal behavior
 * MPI or gather allocation failure cannot safely rejoin and invokes the retained
 * fatal handler before mutation.
 */
StateTransitionResult<void> agree_transition(const std::shared_ptr<const detail::StateCollectiveContext>& context,
                                             const TransitionEnvelope& local)
{
    int size = 0;
    detail::require_state_mpi_success(
        context->run_control, {.operation = MPI_Comm_size(context->communicator, &size), .fatal = MPI_ERR_OTHER});
    std::vector<TransitionEnvelope> gathered(static_cast<std::size_t>(size));
    detail::require_state_mpi_success(
        context->run_control,
        {.operation = MPI_Allgather(&local, static_cast<int>(sizeof(local)), MPI_BYTE, gathered.data(),
                                    static_cast<int>(sizeof(local)), MPI_BYTE, context->communicator),
         .fatal = MPI_ERR_OTHER});
    const auto differs = [&](auto member) {
        return std::ranges::any_of(gathered,
                                   [&](const auto& value) { return value.*member != gathered.front().*member; });
    };
    if (differs(&TransitionEnvelope::operation)) {
        return std::unexpected(StateTransitionError{StateTransitionErrorCode::operation_mismatch});
    }
    if (differs(&TransitionEnvelope::store)) {
        return std::unexpected(StateTransitionError{StateTransitionErrorCode::store_mismatch});
    }
    if (differs(&TransitionEnvelope::subject)) {
        const auto code = local.operation == static_cast<std::uint64_t>(StateOperation::seal) ||
                                  local.operation == static_cast<std::uint64_t>(StateOperation::set_regional_value)
                              ? StateTransitionErrorCode::transaction_mismatch
                              : StateTransitionErrorCode::argument_mismatch;
        return std::unexpected(StateTransitionError{code});
    }
    return agree_transition_state(gathered, local);
}

/**
 * \brief Compare exact binary64 level-set representations on every owner partition.
 * \param left candidate owner-only level-set vector.
 * \param right base owner-only level-set vector on the same partitioner.
 * \param context immutable collective communicator context.
 * \return true only when every owner-local representation matches globally.
 * \par Maintainer workflow
 * Signed zero, nonfinite values, and NaN payloads are compared by representation.
 * MPI failure is fatal.
 */
bool level_sets_equal(const DistributedStateVector& left, const DistributedStateVector& right,
                      const std::shared_ptr<const detail::StateCollectiveContext>& context)
{
    bool local_equal = left.locally_owned_size() == right.locally_owned_size();
    for (dealii::types::global_dof_index index = 0; local_equal && index < left.locally_owned_size(); ++index) {
        local_equal = std::bit_cast<std::uint64_t>(left.local_element(index)) ==
                      std::bit_cast<std::uint64_t>(right.local_element(index));
    }
    int local = local_equal ? 1 : 0;
    int global = 0;
    detail::require_state_mpi_success(
        context->run_control, {.operation = MPI_Allreduce(&local, &global, 1, MPI_INT, MPI_MIN, context->communicator),
                               .fatal = MPI_ERR_OTHER});
    return global == 1;
}

/**
 * \brief Replicate rank-zero regional backend representations before sealing.
 * \param storage transaction-private regional backends and replicated cache.
 * \param context retained communicator and fatal-error authority.
 * \param broadcast exact MPI-compatible bitwise broadcast operation.
 * \param staging exact-bit buffer factory protected by the caller's fatal boundary.
 * \return staged owner-backend representations in regional-entry order.
 * \par Maintainer workflow
 * Call after descriptor agreement and before identity reservation. Rank zero is
 * the sole backend owner. Do not commit the returned bits to the transaction
 * cache until every recoverable seal check and identity reservation succeeds.
 * Allocation and MPI failure are fatal at the caller's stage boundary.
 */
std::vector<std::uint64_t> synchronized_regional_entries(
    const detail::StateVectorStorage& storage, const std::shared_ptr<const detail::StateCollectiveContext>& context,
    const detail::StateRegionalBroadcast broadcast, const detail::StateRegionalStagingFactory staging)
{
    auto synchronized = staging(storage.regional_entries.size());
    for (std::size_t index = 0; index < storage.regional_entries.size(); ++index) {
        const auto& backend = storage.regional_entries.at(index);
        if (backend.locally_owned_size() == 1U) {
            synchronized.at(index) = std::bit_cast<std::uint64_t>(backend.local_element(0));
        }
        detail::require_state_mpi_success(
            context->run_control,
            {.operation = broadcast(&synchronized.at(index), 1, MPI_UINT64_T, 0, context->communicator),
             .fatal = MPI_ERR_OTHER});
    }
    return synchronized;
}

/**
 * \brief Build one local transition envelope for a store snapshot argument.
 * \param authority externally serialized store registry.
 * \param operation begin, publish, discard, pin, or unpin operation.
 * \param argument snapshot argument supplied by this rank.
 * \return fixed descriptor without allocating or mutating the registry.
 */
TransitionEnvelope store_envelope(const detail::StateStoreAuthority& authority, const StateOperation operation,
                                  const StateSnapshotId argument)
{
    const auto found = authority.snapshots.find(argument.value());
    const bool known = found != authority.snapshots.end();
    bool private_candidate = false;
    bool transient = false;
    bool pinned = false;
    bool root_current = false;
    if (known) {
        private_candidate = !found->second.snapshot->stamp.published_epoch.has_value();
        transient = authority.transient == argument.value();
        pinned = found->second.pinned;
        root_current = found->second.snapshot->accepted_root == authority.accepted->stamp.snapshot;
    }
    return {.operation = static_cast<std::uint64_t>(operation),
            .store = authority.context->store.value(),
            .subject = argument.value(),
            .authority = 1,
            .active = 1,
            .known = known ? 1U : 0U,
            .candidate = private_candidate ? 1U : 0U,
            .transient = transient ? 1U : 0U,
            .pinned = pinned ? 1U : 0U,
            .pin_available =
                authority.pinned_count < authority.context->retention.max_pinned_private_snapshots ? 1U : 0U,
            .root_current = root_current ? 1U : 0U};
}

/**
 * \brief Execute an allocation/dependency state stage beneath the fatal boundary.
 * \tparam Operation nullary callable type.
 * \param control retained run fatal authority.
 * \param operation stage callable invoked exactly once.
 * \return the callable result when no exception occurs.
 * \par Fatal behavior
 * `std::bad_alloc` maps to `MPI_ERR_NO_MEM`; every other exception maps to
 * `MPI_ERR_OTHER`. Neither path returns to an unmatched collective protocol.
 */
template<class Operation>
decltype(auto) invoke_state_fatal(const std::shared_ptr<const detail::RunConfigurationControl>& control,
                                  Operation&& operation)
{
    try {
        return std::forward<Operation>(operation)();
    }
    catch (const std::bad_alloc&) {
        abort_state(control, MPI_ERR_NO_MEM);
    }
    catch (...) {
        abort_state(control, MPI_ERR_OTHER);
    }
}

/**
 * \brief Copy the collective tombstone intentionally while moving mutable state.
 * \param tombstone identity retained by both moved-to and moved-from handles.
 * \return one additional shared owner of the same immutable tombstone.
 * \par Maintainer workflow
 * Use only in transaction move operations: the collective protocol requires a
 * moved-from handle to remain able to join a later inactive check.
 */
std::shared_ptr<const detail::StateTransactionTombstone>
retain_tombstone_after_move(const std::shared_ptr<const detail::StateTransactionTombstone>& tombstone)
{
    return tombstone;
}

} // namespace

/**
 * \brief Compare complete local field provenance and identity.
 * \param left first field reference.
 * \param right second field reference.
 * \return true only when space, optional phase, and group all match.
 */
bool operator==(const StateFieldReference& left, const StateFieldReference& right)
{
    const auto phase_key = [](const std::optional<PhaseReference>& phase) {
        return phase ? std::optional{std::tuple{phase->graph.run, phase->graph.graph, phase->phase}} : std::nullopt;
    };
    return left.space == right.space && phase_key(left.phase) == phase_key(right.phase) && left.group == right.group;
}

std::string_view StateTransitionError::message() const noexcept
{
    static constexpr std::array messages{
        std::string_view{"ranks entered different state operations"},
        std::string_view{"ranks supplied different state stores"},
        std::string_view{"ranks supplied different state transactions"},
        std::string_view{"ranks supplied different state arguments"},
        std::string_view{"the replicated state layout differs between ranks"},
        std::string_view{"the replicated state retention policy differs between ranks"},
        std::string_view{"the state store authority has expired"},
        std::string_view{"the state transaction is inactive"},
        std::string_view{"the state snapshot is not retained by this store"},
        std::string_view{"only a private state candidate can be published"},
        std::string_view{"only a retained private state candidate can be discarded"},
        std::string_view{"the private snapshot pin limit has been reached"},
        std::string_view{"the private candidate descends from a stale accepted root"},
        std::string_view{"the finite collective state identity sequence is exhausted"},
        std::string_view{"the field reference belongs to another space or phase"},
        std::string_view{"the field identity does not exist in this state layout"},
        std::string_view{"the regional scalar identity does not exist in this state layout"},
    };
    static_assert(messages.size() == static_cast<std::size_t>(StateTransitionErrorCode::unknown_regional_entry) + 1U);
    return messages.at(static_cast<std::size_t>(code));
}

StateSnapshotStamp StateSnapshot::stamp() const noexcept { return data_->stamp; }

LevelSetFieldSetSnapshotId StateSnapshot::level_set_snapshot() const noexcept { return data_->level_set_snapshot; }

double StateSnapshot::regional_value(const RegionalEntryId entry) const
{
    if (entry.value() >= data_->storage->regional_cache.size()) {
        throw std::out_of_range("the regional scalar identity does not exist in this state layout");
    }
    return std::bit_cast<double>(data_->storage->regional_cache.at(entry.value()));
}

StateTransitionResult<std::reference_wrapper<const DistributedStateVector>>
StateSnapshot::field(const StateFieldReference reference) const noexcept
{
    if (reference.space != data_->context->provenance) {
        return std::unexpected(StateTransitionError{StateTransitionErrorCode::field_reference_mismatch});
    }
    if (reference.group.value() >= data_->context->fields.size()) {
        return std::unexpected(StateTransitionError{StateTransitionErrorCode::unknown_field});
    }
    if (!(data_->context->fields.at(reference.group.value()) == reference)) {
        return std::unexpected(StateTransitionError{StateTransitionErrorCode::field_reference_mismatch});
    }
    return std::cref(data_->storage->fields.at(reference.group.value()));
}

MutableStateTransaction::MutableStateTransaction(std::shared_ptr<const detail::StateTransactionTombstone> tombstone,
                                                 std::unique_ptr<detail::MutableStateData> data) :
    tombstone_(std::move(tombstone)), data_(std::move(data))
{
}

MutableStateTransaction::MutableStateTransaction(MutableStateTransaction&& other) noexcept :
    tombstone_(retain_tombstone_after_move(other.tombstone_)), data_(std::move(other.data_))
{
}

MutableStateTransaction& MutableStateTransaction::operator=(MutableStateTransaction&& other) noexcept
{
    if (this != &other) {
        tombstone_ = other.tombstone_;
        data_ = std::move(other.data_);
    }
    return *this;
}

MutableStateTransaction::~MutableStateTransaction() = default;

StateTransitionResult<std::reference_wrapper<DistributedStateVector>>
MutableStateTransaction::field(const StateFieldReference reference) noexcept
{
    if (tombstone_->authority.expired()) {
        return std::unexpected(StateTransitionError{StateTransitionErrorCode::expired_store});
    }
    if (!data_) {
        return std::unexpected(StateTransitionError{StateTransitionErrorCode::inactive_transaction});
    }
    if (reference.space != tombstone_->context->provenance) {
        return std::unexpected(StateTransitionError{StateTransitionErrorCode::field_reference_mismatch});
    }
    if (reference.group.value() >= tombstone_->context->fields.size()) {
        return std::unexpected(StateTransitionError{StateTransitionErrorCode::unknown_field});
    }
    if (!(tombstone_->context->fields.at(reference.group.value()) == reference)) {
        return std::unexpected(StateTransitionError{StateTransitionErrorCode::field_reference_mismatch});
    }
    return std::ref(data_->storage->fields.at(reference.group.value()));
}

StateTransitionResult<void> MutableStateTransaction::set_regional_value_collective(const RegionalEntryId entry,
                                                                                   const double value)
{
    const auto authority = tombstone_->authority.lock();
    const auto bits = std::bit_cast<std::uint64_t>(value);
    const bool known = data_ && entry.value() < data_->storage->regional_cache.size();
    const TransitionEnvelope envelope{
        .operation = static_cast<std::uint64_t>(StateOperation::set_regional_value),
        .store = tombstone_->context->store.value(),
        .subject = tombstone_->transaction.value(),
        .entry = entry.value(),
        .value_bits = bits,
        .authority = authority ? 1U : 0U,
        .active = data_ ? 1U : 0U,
        .known = known ? 1U : 0U,
    };
    auto agreement = invoke_state_fatal(tombstone_->context->run_control,
                                        [&] { return agree_transition(tombstone_->context, envelope); });
    if (!agreement) {
        return std::unexpected(agreement.error());
    }
    auto& storage = *data_->storage;
    storage.regional_cache.at(entry.value()) = bits;
    auto& backend = storage.regional_entries.at(entry.value());
    if (backend.locally_owned_size() == 1U) {
        backend.local_element(0) = value;
    }
    return {};
}

StateTransitionResult<StateSnapshot> MutableStateTransaction::seal_collective()
{
    return seal_collective_with_hook(detail::no_state_post_agreement_hook, MPI_Bcast,
                                     detail::make_state_regional_staging);
}

StateTransitionResult<StateSnapshot>
MutableStateTransaction::seal_collective_with_hook(const detail::StatePostAgreementHook hook,
                                                   const detail::StateRegionalBroadcast broadcast,
                                                   const detail::StateRegionalStagingFactory staging)
{
    const auto authority = tombstone_->authority.lock();
    const TransitionEnvelope envelope{
        .operation = static_cast<std::uint64_t>(StateOperation::seal),
        .store = tombstone_->context->store.value(),
        .subject = tombstone_->transaction.value(),
        .authority = authority ? 1U : 0U,
        .active = data_ ? 1U : 0U,
        .known = 1,
        .candidate = 1,
        .root_current = 1,
    };
    auto agreement = invoke_state_fatal(tombstone_->context->run_control,
                                        [&] { return agree_transition(tombstone_->context, envelope); });
    if (!agreement) {
        return std::unexpected(agreement.error());
    }
    invoke_state_fatal(tombstone_->context->run_control, hook);
    auto regional_bits = invoke_state_fatal(tombstone_->context->run_control, [&] {
        return synchronized_regional_entries(*data_->storage, tombstone_->context, broadcast, staging);
    });
    const auto level_group = tombstone_->context->level_set_group;
    const bool unchanged = level_sets_equal(data_->storage->fields.at(level_group.value()),
                                            data_->base->storage->fields.at(level_group.value()), tombstone_->context);
    const auto mask = snapshot_mask | (unchanged ? 0U : level_set_mask);
    auto identities = reserve_state_identities(tombstone_->context->run_control, tombstone_->context->communicator,
                                               tombstone_->context->provenance.mesh, mask);
    if (!identities) {
        return std::unexpected(identities.error());
    }
    data_->storage->regional_cache = std::move(regional_bits);
    return invoke_state_fatal(tombstone_->context->run_control, [&]() -> StateTransitionResult<StateSnapshot> {
        auto storage = std::shared_ptr<const detail::StateVectorStorage>(std::move(data_->storage));
        const auto level_set =
            unchanged ? data_->base->level_set_snapshot : LevelSetFieldSetSnapshotId::from_index(identities->level_set);
        auto candidate = std::make_shared<detail::StateSnapshotData>(detail::StateSnapshotData{
            .context = tombstone_->context,
            .stamp = StateSnapshotStamp(tombstone_->context->provenance, tombstone_->context->store,
                                        StateSnapshotId::from_index(identities->snapshot), std::nullopt),
            .level_set_snapshot = level_set,
            .accepted_root = data_->accepted_root,
            .storage = std::move(storage),
        });
        if (authority->transient) {
            authority->snapshots.erase(*authority->transient);
        }
        authority->transient = candidate->stamp.snapshot.value();
        authority->snapshots.emplace(candidate->stamp.snapshot.value(),
                                     detail::StateStoreAuthority::SnapshotRecord{.snapshot = candidate});
        data_.reset();
        return StateSnapshot(std::move(candidate));
    });
}

void MutableStateTransaction::abandon() noexcept { data_.reset(); }

bool MutableStateTransaction::active() const noexcept { return data_ != nullptr && !tombstone_->authority.expired(); }

StateTransactionId MutableStateTransaction::id() const noexcept { return tombstone_->transaction; }

StateStore::StateStore(std::shared_ptr<detail::StateStoreAuthority> authority) : authority_(std::move(authority)) {}

StateStore::StateStore(StateStore&&) noexcept = default;

StateStore::~StateStore() = default;

StateSnapshot StateStore::snapshot(const StateSlot slot) const
{
    if (slot == StateSlot::accepted) {
        return StateSnapshot(authority_->accepted);
    }
    if (!authority_->previous) {
        throw std::out_of_range("the state store has no previous accepted snapshot");
    }
    return StateSnapshot(authority_->previous);
}

StateSnapshot StateStore::snapshot(const StateSnapshotId id) const
{
    const auto found = authority_->snapshots.find(id.value());
    if (found == authority_->snapshots.end()) {
        throw std::out_of_range("state snapshot identity is not retained by this store");
    }
    return StateSnapshot(found->second.snapshot);
}

StateTransitionResult<MutableStateTransaction> StateStore::begin_trial_collective(const StateSnapshotId base)
{
    return begin_trial_collective_with_hook(base, detail::no_state_post_agreement_hook);
}

StateTransitionResult<MutableStateTransaction>
StateStore::begin_trial_collective_with_hook(const StateSnapshotId base, const detail::StatePostAgreementHook hook)
{
    const auto envelope = store_envelope(*authority_, StateOperation::begin, base);
    auto agreement = invoke_state_fatal(authority_->context->run_control,
                                        [&] { return agree_transition(authority_->context, envelope); });
    if (!agreement) {
        return std::unexpected(agreement.error());
    }
    invoke_state_fatal(authority_->context->run_control, hook);
    auto identities = reserve_state_identities(authority_->context->run_control, authority_->context->communicator,
                                               authority_->context->provenance.mesh, transaction_mask);
    if (!identities) {
        return std::unexpected(identities.error());
    }
    return invoke_state_fatal(
        authority_->context->run_control, [&]() -> StateTransitionResult<MutableStateTransaction> {
            const auto base_data = authority_->snapshots.at(base.value()).snapshot;
            const auto root = base_data->stamp.published_epoch ? base_data->stamp.snapshot : base_data->accepted_root;
            auto data = std::make_unique<detail::MutableStateData>(detail::MutableStateData{
                .base = base_data,
                .accepted_root = root,
                .storage = std::make_unique<detail::StateVectorStorage>(*base_data->storage),
            });
            auto tombstone = std::make_shared<detail::StateTransactionTombstone>(detail::StateTransactionTombstone{
                .context = authority_->context,
                .authority = authority_,
                .transaction = StateTransactionId::from_index(identities->transaction),
                .base = base});
            return MutableStateTransaction(std::move(tombstone), std::move(data));
        });
}

StateTransitionResult<StateSnapshot> StateStore::publish_collective(const StateSnapshotId candidate)
{
    return publish_collective_with_hook(candidate, detail::no_state_post_agreement_hook);
}

StateTransitionResult<StateSnapshot> StateStore::publish_collective_with_hook(const StateSnapshotId candidate,
                                                                              const detail::StatePostAgreementHook hook)
{
    const auto envelope = store_envelope(*authority_, StateOperation::publish, candidate);
    auto agreement = invoke_state_fatal(authority_->context->run_control,
                                        [&] { return agree_transition(authority_->context, envelope); });
    if (!agreement) {
        return std::unexpected(agreement.error());
    }
    invoke_state_fatal(authority_->context->run_control, hook);
    auto identities = reserve_state_identities(authority_->context->run_control, authority_->context->communicator,
                                               authority_->context->provenance.mesh, epoch_mask);
    if (!identities) {
        return std::unexpected(identities.error());
    }
    return invoke_state_fatal(authority_->context->run_control, [&]() -> StateTransitionResult<StateSnapshot> {
        const auto source = authority_->snapshots.at(candidate.value()).snapshot;
        auto published = std::make_shared<detail::StateSnapshotData>(*source);
        published->stamp.published_epoch = StateEpoch::from_index(identities->epoch);
        published->accepted_root = published->stamp.snapshot;
        auto& candidate_record = authority_->snapshots.at(candidate.value());
        if (candidate_record.pinned) {
            --authority_->pinned_count;
        }
        else {
            authority_->transient.reset();
        }
        const auto evicted = authority_->previous;
        authority_->previous = authority_->accepted;
        authority_->accepted = published;
        candidate_record = detail::StateStoreAuthority::SnapshotRecord{.snapshot = published};
        if (evicted) {
            authority_->snapshots.erase(evicted->stamp.snapshot.value());
        }
        return StateSnapshot(std::move(published));
    });
}

StateTransitionResult<void> StateStore::pin_collective(const StateSnapshotId candidate)
{
    const auto envelope = store_envelope(*authority_, StateOperation::pin, candidate);
    auto agreement = invoke_state_fatal(authority_->context->run_control,
                                        [&] { return agree_transition(authority_->context, envelope); });
    if (!agreement) {
        return std::unexpected(agreement.error());
    }
    auto& record = authority_->snapshots.at(candidate.value());
    if (record.pinned) {
        return {};
    }
    record.pinned = true;
    authority_->transient.reset();
    ++authority_->pinned_count;
    return {};
}

StateTransitionResult<void> StateStore::unpin_collective(const StateSnapshotId candidate)
{
    const auto envelope = store_envelope(*authority_, StateOperation::unpin, candidate);
    auto agreement = invoke_state_fatal(authority_->context->run_control,
                                        [&] { return agree_transition(authority_->context, envelope); });
    if (!agreement) {
        return std::unexpected(agreement.error());
    }
    auto& record = authority_->snapshots.at(candidate.value());
    if (!record.pinned) {
        return {};
    }
    if (authority_->transient) {
        authority_->snapshots.erase(*authority_->transient);
    }
    record.pinned = false;
    authority_->transient = candidate.value();
    --authority_->pinned_count;
    return {};
}

StateTransitionResult<void> StateStore::discard_collective(const StateSnapshotId candidate)
{
    const auto envelope = store_envelope(*authority_, StateOperation::discard, candidate);
    auto agreement = invoke_state_fatal(authority_->context->run_control,
                                        [&] { return agree_transition(authority_->context, envelope); });
    if (!agreement) {
        return std::unexpected(agreement.error());
    }
    authority_->snapshots.erase(candidate.value());
    authority_->transient.reset();
    return {};
}

SpaceEpoch StateStore::space_epoch() const noexcept { return authority_->context->provenance.epoch; }

SpaceProvenance StateStore::provenance() const noexcept { return authority_->context->provenance; }

StateStoreId StateStore::id() const noexcept { return authority_->context->store; }

StateRetentionPolicy StateStore::retention_policy() const noexcept { return authority_->context->retention; }

StateTransitionResult<StateStore> make_state_store(const StateLayout& layout, const StateRetentionPolicy retention)
{
    return detail::StateStoreAccess::make_state_store(layout, retention, detail::no_state_post_agreement_hook);
}

StateTransitionResult<StateStore> detail::StateStoreAccess::make_state_store(const StateLayout& layout,
                                                                             const StateRetentionPolicy retention,
                                                                             const StatePostAgreementHook hook)
{
    const auto control = detail::StateLayoutAccess::run_control(layout);
    return invoke_state_fatal(control, [&]() -> StateTransitionResult<StateStore> {
        const auto schema = canonical_layout(layout);
        if (!collectively_equal(control, control->communicator(), schema)) {
            return std::unexpected(StateTransitionError{StateTransitionErrorCode::replicated_layout_mismatch});
        }
        const auto policy = std::to_string(retention.max_pinned_private_snapshots);
        if (!collectively_equal(control, control->communicator(), policy)) {
            return std::unexpected(StateTransitionError{StateTransitionErrorCode::retention_policy_mismatch});
        }
        hook();
        auto identities = reserve_state_identities(control, layout.communicator(), layout.provenance().mesh,
                                                   store_mask | snapshot_mask | epoch_mask | level_set_mask);
        if (!identities) {
            return std::unexpected(identities.error());
        }
        auto context = std::make_shared<detail::StateCollectiveContext>(detail::StateCollectiveContext{
            .run_control = control,
            .mesh_lifetime = detail::StateLayoutAccess::mesh_lifetime(layout),
            .provenance = layout.provenance(),
            .communicator = layout.communicator(),
            .store = StateStoreId::from_index(identities->store),
            .retention = retention,
            .fields = {},
            .level_set_group = layout.level_set_group(),
        });
        context->fields.reserve(layout.field_blocks().size());
        for (const auto& block : layout.field_blocks()) {
            context->fields.push_back(
                StateFieldReference{.space = layout.provenance(), .phase = block.phase, .group = block.id});
        }
        auto storage = std::make_shared<detail::StateVectorStorage>();
        storage->fields.resize(layout.field_blocks().size());
        for (const auto& block : layout.field_blocks()) {
            storage->fields.at(block.id.value()).reinit(block.locally_owned_dofs, layout.communicator());
        }
        storage->regional_entries.resize(layout.regional_entries().size());
        storage->regional_cache.resize(layout.regional_entries().size());
        for (const auto& entry : layout.regional_entries()) {
            storage->regional_entries.at(entry.id.value()).reinit(entry.locally_owned_entries, layout.communicator());
        }
        auto initial = std::make_shared<detail::StateSnapshotData>(detail::StateSnapshotData{
            .context = context,
            .stamp = StateSnapshotStamp(layout.provenance(), context->store,
                                        StateSnapshotId::from_index(identities->snapshot),
                                        StateEpoch::from_index(identities->epoch)),
            .level_set_snapshot = LevelSetFieldSetSnapshotId::from_index(identities->level_set),
            .accepted_root = StateSnapshotId::from_index(identities->snapshot),
            .storage = std::move(storage),
        });
        const auto initial_id = initial->stamp.snapshot.value();
        auto authority =
            std::make_shared<detail::StateStoreAuthority>(detail::StateStoreAuthority{.context = std::move(context),
                                                                                      .snapshots = {},
                                                                                      .accepted = initial,
                                                                                      .previous = {},
                                                                                      .transient = {},
                                                                                      .pinned_count = 0});
        authority->snapshots.emplace(initial_id,
                                     detail::StateStoreAuthority::SnapshotRecord{.snapshot = std::move(initial)});
        return StateStore(std::move(authority));
    });
}

StateTransitionResult<MutableStateTransaction>
detail::StateStoreAccess::begin_trial(StateStore& store, const StateSnapshotId base, const StatePostAgreementHook hook)
{
    return store.begin_trial_collective_with_hook(base, hook);
}

StateTransitionResult<StateSnapshot> detail::StateStoreAccess::seal(MutableStateTransaction& transaction,
                                                                    const StatePostAgreementHook hook)
{
    return transaction.seal_collective_with_hook(hook, MPI_Bcast, make_state_regional_staging);
}

StateTransitionResult<StateSnapshot>
detail::StateStoreAccess::seal_with_regional_broadcast(MutableStateTransaction& transaction,
                                                       const StateRegionalBroadcast broadcast)
{
    return transaction.seal_collective_with_hook(no_state_post_agreement_hook, broadcast, make_state_regional_staging);
}

StateTransitionResult<StateSnapshot>
detail::StateStoreAccess::seal_with_regional_staging(MutableStateTransaction& transaction,
                                                     const StateRegionalStagingFactory staging)
{
    return transaction.seal_collective_with_hook(no_state_post_agreement_hook, MPI_Bcast, staging);
}

StateTransitionResult<StateSnapshot>
detail::StateStoreAccess::publish(StateStore& store, const StateSnapshotId candidate, const StatePostAgreementHook hook)
{
    return store.publish_collective_with_hook(candidate, hook);
}

std::uint64_t detail::StateStoreAccess::regional_cache_bits(const StateSnapshot& snapshot, const RegionalEntryId entry)
{
    return snapshot.data_->storage->regional_cache.at(entry.value());
}

std::optional<std::uint64_t> detail::StateStoreAccess::regional_backend_bits(const StateSnapshot& snapshot,
                                                                             const RegionalEntryId entry)
{
    const auto& backend = snapshot.data_->storage->regional_entries.at(entry.value());
    if (backend.locally_owned_size() == 0U) {
        return std::nullopt;
    }
    return std::bit_cast<std::uint64_t>(backend.local_element(0));
}

std::uint64_t detail::StateStoreAccess::regional_cache_bits(const MutableStateTransaction& transaction,
                                                            const RegionalEntryId entry)
{
    return transaction.data_->storage->regional_cache.at(entry.value());
}

std::optional<std::uint64_t> detail::StateStoreAccess::regional_backend_bits(const MutableStateTransaction& transaction,
                                                                             const RegionalEntryId entry)
{
    const auto& backend = transaction.data_->storage->regional_entries.at(entry.value());
    if (backend.locally_owned_size() == 0U) {
        return std::nullopt;
    }
    return std::bit_cast<std::uint64_t>(backend.local_element(0));
}

void detail::StateStoreAccess::set_regional_backend_bits_for_test(MutableStateTransaction& transaction,
                                                                  const RegionalEntryId entry, const std::uint64_t bits)
{
    auto& backend = transaction.data_->storage->regional_entries.at(entry.value());
    if (backend.locally_owned_size() == 1U) {
        backend.local_element(0) = std::bit_cast<double>(bits);
    }
}

std::array<std::uint64_t, 5>
detail::StateStoreAccess::replace_identity_sequences_for_test(std::array<std::uint64_t, 5> replacement)
{
    auto& sequences = state_identity_sequences();
    const std::scoped_lock lock(sequences.mutex);
    std::swap(sequences.next, replacement);
    return replacement;
}

} // namespace rift
