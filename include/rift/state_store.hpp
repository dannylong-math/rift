#pragma once

/**
 * \file
 * \brief Collective state allocation, retention, and publication authority.
 */

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <rift/state_transaction.hpp>
#include <string>
#include <vector>

namespace rift {

class RiftContext;

/** \brief Bound the number of snapshots protected outside mandatory roles. */
struct RetentionPolicy {
    /** \brief Maximum simultaneously pinned snapshot identities. */
    std::size_t max_pinned_snapshots = 0;
};

/** \brief Classify one recoverable state-store construction defect. */
enum class StateStoreCreationErrorCode : std::uint8_t {
    /** \brief One rank supplied no finalized space. */
    null_space,
    /** \brief One rank supplied a space finalized by another context. */
    foreign_space,
    /** \brief Ranks supplied different spaces, dimensions, policies, or counters. */
    collective_space_mismatch,
    /** \brief The phase-label encoding cannot represent every canonical phase. */
    phase_label_encoding_overflow,
    /** \brief The context cannot represent another store identity. */
    state_store_id_exhausted,
};

/** \brief Describe one state-store construction failure. */
struct StateStoreCreationError {
    /** \brief Machine-readable failure classification. */
    StateStoreCreationErrorCode code;
    /** \brief World rank whose input caused the defect. */
    unsigned int rank;
    /** \brief Human-readable diagnostic. */
    std::string message;
};

/** \brief Complete deterministic collective store-construction errors. */
using StateStoreCreationErrors = std::vector<StateStoreCreationError>;

/**
 * \brief Own accepted-state publication and bounded snapshot retention.
 *
 * The uniquely owned store shares immutable snapshots with callers. Store
 * transitions are collective and externally serialized; snapshot reads are
 * local and may proceed concurrently.
 */
template<int dim>
    requires(dim == 2 || dim == 3)
class StateStore {
public:
    /** \brief Release store authority without invalidating external snapshots. */
    ~StateStore();
    StateStore(const StateStore&) = delete;
    StateStore& operator=(const StateStore&) = delete;
    StateStore(StateStore&&) = delete;
    StateStore& operator=(StateStore&&) = delete;

    /** \brief Return this context-local store identity. */
    [[nodiscard]] StateStoreId id() const noexcept;
    /** \brief Return the finalized space retained by this store. */
    [[nodiscard]] const SpaceSnapshot<dim>& space() const noexcept;
    /** \brief Return the always-present current accepted snapshot. */
    [[nodiscard]] std::shared_ptr<const StateSnapshot<dim>> accepted() const noexcept;
    /** \brief Return the immediately preceding accepted snapshot, if any. */
    [[nodiscard]] std::shared_ptr<const StateSnapshot<dim>> previous() const noexcept;
    /** \brief Return the most recently sealed unpinned candidate, if any. */
    [[nodiscard]] std::shared_ptr<const StateSnapshot<dim>> transient() const noexcept;

    /** \brief Collectively clone the current accepted state into a transaction. */
    [[nodiscard]] StateTransactionResult<dim> begin_transaction();

    /** \brief Resolve an indexed snapshot locally without communicating. */
    [[nodiscard]] std::expected<std::shared_ptr<const StateSnapshot<dim>>, StateError>
    lookup(StateSnapshotReference reference) const;

    /** \brief Collectively accept a current-root sealed candidate. */
    [[nodiscard]] std::expected<std::shared_ptr<const StateSnapshot<dim>>, StateErrors>
    publish(StateSnapshotReference reference);
    /** \brief Collectively protect an indexed snapshot within configured capacity. */
    [[nodiscard]] std::expected<void, StateErrors> pin(StateSnapshotReference reference);
    /** \brief Collectively release explicit protection from an indexed snapshot. */
    [[nodiscard]] std::expected<void, StateErrors> unpin(StateSnapshotReference reference);
    /** \brief Collectively remove an unpinned private candidate from the index. */
    [[nodiscard]] std::expected<void, StateErrors> discard(StateSnapshotReference reference);

private:
    friend class RiftContext;
    friend class StateTransaction<dim>;

    /** \brief Allocate the deterministic root after collective preflight. */
    StateStore(const RiftContext* creator_context, StateStoreId id, std::shared_ptr<const SpaceSnapshot<dim>> space,
               RetentionPolicy policy);

    /** \brief Stable shared control observed weakly by transactions. */
    std::shared_ptr<detail::StateStoreControl<dim>> control_;
};

/** \brief Result of collectively allocating one state store. */
template<int dim>
    requires(dim == 2 || dim == 3)
using StateStoreResult = std::expected<std::unique_ptr<StateStore<dim>>, StateStoreCreationErrors>;

} // namespace rift
