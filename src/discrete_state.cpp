/**
 * \file
 * \brief Central state-vector allocation, immutable snapshots, and trial transactions.
 */

#include <atomic>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <deal.II/base/types.h>
#include <map>
#include <memory>
#include <mpi.h>
#include <optional>
#include <rift/discrete_state.hpp>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rift {

namespace detail {

namespace {

/**
 * \brief Own every distributed field and regional vector in one state revision.
 *
 * Maintainers copy this storage only when opening a mutable transaction.
 * Immutable snapshots and publication stamps share it read-only.
 */
struct StateVectorStorage {
    /** \brief Field vectors indexed by `FieldGroupId`. */
    std::vector<DistributedStateVector> fields;
    /** \brief Regional scalar vectors indexed by `RegionalEntryId`. */
    std::vector<DistributedStateVector> regional_entries;
};

} // namespace

/**
 * \brief Pair immutable vector storage with every identity needed for safe reuse.
 *
 * \par When to use
 * `StateStore` creates this maintainer-facing aggregate for the initial state,
 * every sealed private trial, and every published revision. Public code reads
 * it through `StateSnapshot`.
 *
 * \par Typical use
 * \code{.cpp}
 * auto storage = std::make_shared<const detail::StateVectorStorage>();
 * auto snapshot = std::make_shared<const detail::StateSnapshotData>(
 *     detail::StateSnapshotData{stamp, level_set_revision, storage});
 * \endcode
 *
 * \par Important behavior
 * A private candidate has no published epoch. Publication copies this small
 * metadata object, preserves its `StateSnapshotId`, assigns `StateEpoch`, and
 * continues sharing the immutable vector storage.
 */
struct StateSnapshotData {
    /** \brief Space, snapshot, and optional accepted revision identities. */
    StateSnapshotStamp stamp;
    /** \brief Revision of the geometry-field values in `storage`. */
    LevelSetFieldSetSnapshotId level_set_snapshot;
    /** \brief Immutable centrally owned vector storage. */
    std::shared_ptr<const StateVectorStorage> storage;
};

/**
 * \brief Keep the immutable trial base beside its transaction-private vector copy.
 *
 * \par When to use
 * `StateStore::begin_trial()` creates this aggregate and transfers it to one
 * move-only `MutableStateTransaction`. Sealing or abandonment destroys its
 * mutable ownership.
 *
 * \par Typical use
 * \code{.cpp}
 * auto mutable_data = std::make_unique<detail::MutableStateData>(
 *     detail::MutableStateData{
 *         base_snapshot,
 *         std::make_unique<detail::StateVectorStorage>(*base_snapshot->storage)});
 * \endcode
 *
 * \par Important behavior
 * Retaining `base` permits exact level-set comparison at seal time without
 * exposing a mutable alias to accepted or previously sealed storage.
 */
struct MutableStateData {
    /** \brief Immutable snapshot copied when the transaction began. */
    std::shared_ptr<const StateSnapshotData> base;
    /** \brief Transaction-private vector copies. */
    std::unique_ptr<StateVectorStorage> storage;
};

} // namespace detail

namespace {

/** \brief Allocate a process-local unique state-snapshot identity. */
StateSnapshotId reserve_state_snapshot_id()
{
    static std::atomic<std::uint64_t> next{0};
    return StateSnapshotId::from_index(next.fetch_add(1, std::memory_order_relaxed));
}

/** \brief Allocate a process-local unique accepted-state revision. */
StateEpoch reserve_state_epoch()
{
    static std::atomic<std::uint64_t> next{0};
    return StateEpoch::from_index(next.fetch_add(1, std::memory_order_relaxed));
}

/** \brief Allocate a process-local unique level-set field revision. */
LevelSetFieldSetSnapshotId reserve_level_set_snapshot_id()
{
    static std::atomic<std::uint64_t> next{0};
    return LevelSetFieldSetSnapshotId::from_index(next.fetch_add(1, std::memory_order_relaxed));
}

/**
 * \brief Compare locally owned vector values collectively across the layout communicator.
 *
 * \param left first distributed vector.
 * \param right second distributed vector with the same partition.
 * \param communicator communicator shared by both vectors.
 * \return whether every locally owned value is exactly equal on every rank.
 */
bool vectors_equal(const DistributedStateVector& left, const DistributedStateVector& right, const MPI_Comm communicator)
{
    bool local_equal = left.locally_owned_size() == right.locally_owned_size();
    for (dealii::types::global_dof_index index = 0; local_equal && index < left.locally_owned_size(); ++index) {
        local_equal = left.local_element(index) == right.local_element(index);
    }
    return dealii::Utilities::MPI::min(local_equal ? 1U : 0U, communicator) == 1U;
}

} // namespace

/**
 * \brief Retain the finalized layout, immutable snapshots, and publication slots.
 *
 * The raw integer map key is internal; callers can only supply and receive the
 * corresponding strong identifier.
 */
struct StateStore::Impl {
    /** \brief Finalized vector layout owned by this store. */
    StateLayout layout;
    /** \brief Every accepted, previous, or private snapshot still retained. */
    std::map<std::uint64_t, std::shared_ptr<const detail::StateSnapshotData>> snapshots;
    /** \brief Current live accepted snapshot. */
    std::shared_ptr<const detail::StateSnapshotData> accepted;
    /** \brief Accepted snapshot preceding `accepted`, when one exists. */
    std::shared_ptr<const detail::StateSnapshotData> previous;
};

StateSnapshotStamp StateSnapshot::stamp() const noexcept { return data_->stamp; }

LevelSetFieldSetSnapshotId StateSnapshot::level_set_snapshot() const noexcept { return data_->level_set_snapshot; }

const DistributedStateVector& StateSnapshot::field(const FieldGroupId group) const
{
    return data_->storage->fields.at(group.value());
}

const DistributedStateVector& StateSnapshot::regional(const RegionalEntryId entry) const
{
    return data_->storage->regional_entries.at(entry.value());
}

MutableStateTransaction::MutableStateTransaction(StateStore& owner, std::unique_ptr<detail::MutableStateData> data) :
    owner_(&owner), data_(std::move(data))
{
}

MutableStateTransaction::MutableStateTransaction(MutableStateTransaction&& other) noexcept :
    owner_(std::exchange(other.owner_, nullptr)), data_(std::move(other.data_))
{
}

MutableStateTransaction& MutableStateTransaction::operator=(MutableStateTransaction&& other) noexcept
{
    if (this != &other) {
        owner_ = std::exchange(other.owner_, nullptr);
        data_ = std::move(other.data_);
    }
    return *this;
}

MutableStateTransaction::~MutableStateTransaction() = default;

DistributedStateVector& MutableStateTransaction::field(const FieldGroupId group)
{ // GCOVR_EXCL_LINE -- Clang maps an unreachable entry cleanup block to this opening brace.
    if (!data_) {
        throw std::logic_error("state transaction is no longer active");
    }
    return data_->storage->fields.at(group.value());
} // GCOVR_EXCL_LINE -- Clang maps an unreachable return cleanup block to this closing brace.

DistributedStateVector& MutableStateTransaction::regional(const RegionalEntryId entry)
{
    if (!data_) {
        throw std::logic_error("state transaction is no longer active");
    }
    return data_->storage->regional_entries.at(entry.value());
} // GCOVR_EXCL_LINE -- Clang maps an unreachable return cleanup block to this closing brace.

StateSnapshot MutableStateTransaction::seal()
{
    if (owner_ == nullptr) {
        throw std::logic_error("state transaction is no longer active");
    }
    return owner_->seal(*this);
} // GCOVR_EXCL_LINE -- Clang maps an unreachable return cleanup block to this closing brace.

void MutableStateTransaction::abandon() noexcept
{
    data_.reset();
    owner_ = nullptr;
}

bool MutableStateTransaction::active() const noexcept { return data_ != nullptr; }

StateStore::StateStore(StateLayout layout) :
    impl_(std::make_unique<Impl>(Impl{.layout = std::move(layout), .snapshots = {}, .accepted = {}, .previous = {}}))
{
    auto storage = std::make_shared<detail::StateVectorStorage>();
    storage->fields.resize(impl_->layout.field_blocks().size());
    for (const auto& block : impl_->layout.field_blocks()) {
        storage->fields.at(block.id.value()).reinit(block.locally_owned_dofs, impl_->layout.communicator());
    }

    storage->regional_entries.resize(impl_->layout.regional_entries().size());
    for (const auto& entry : impl_->layout.regional_entries()) {
        storage->regional_entries.at(entry.id.value())
            .reinit(entry.locally_owned_entries, impl_->layout.communicator());
    }

    auto initial = std::make_shared<detail::StateSnapshotData>(detail::StateSnapshotData{
        .stamp = {.space = impl_->layout.space_epoch(),
                  .snapshot = reserve_state_snapshot_id(),
                  .published_epoch = reserve_state_epoch()},
        .level_set_snapshot = reserve_level_set_snapshot_id(),
        .storage = std::move(storage),
    });
    impl_->accepted = initial;
    impl_->snapshots.emplace(initial->stamp.snapshot.value(), std::move(initial));
}

StateStore::~StateStore() = default;
// GCOVR_EXCL_LINE -- Clang maps defaulted-destructor cleanup to the following source line.

StateSnapshot StateStore::snapshot(const StateSlot slot) const
{
    if (slot == StateSlot::accepted) {
        return StateSnapshot(impl_->accepted);
    }
    if (!impl_->previous) {
        throw std::out_of_range("the state store has no previous accepted snapshot");
    }
    return StateSnapshot(impl_->previous);
}

StateSnapshot StateStore::snapshot(const StateSnapshotId id) const
{
    const auto found = impl_->snapshots.find(id.value());
    if (found == impl_->snapshots.end()) {
        throw std::out_of_range("state snapshot identity is not retained by this store");
    }
    return StateSnapshot(found->second);
} // GCOVR_EXCL_LINE -- Clang maps an unreachable return cleanup block to this closing brace.

MutableStateTransaction StateStore::begin_trial(const StateSnapshotId base)
{
    const auto base_snapshot = snapshot(base).data_;
    auto data = std::make_unique<detail::MutableStateData>(detail::MutableStateData{
        .base = base_snapshot,
        .storage = std::make_unique<detail::StateVectorStorage>(*base_snapshot->storage),
    });
    return {*this, std::move(data)};
}

StateSnapshot StateStore::publish(const StateSnapshotId candidate)
{
    const auto source = snapshot(candidate).data_;
    if (source->stamp.published_epoch) {
        throw std::logic_error("only a private state snapshot can be published");
    }

    auto published = std::make_shared<detail::StateSnapshotData>(*source);
    published->stamp.published_epoch = reserve_state_epoch();
    impl_->previous = impl_->accepted;
    impl_->accepted = published;
    impl_->snapshots[candidate.value()] = published;
    return StateSnapshot(std::move(published));
}

void StateStore::discard(const StateSnapshotId candidate)
{
    const auto found = impl_->snapshots.find(candidate.value());
    if (found == impl_->snapshots.end()) {
        throw std::out_of_range("state snapshot identity is not retained by this store");
    }
    if (found->second->stamp.published_epoch) {
        throw std::logic_error("published state snapshots cannot be discarded");
    }
    impl_->snapshots.erase(found);
}

SpaceEpoch StateStore::space_epoch() const noexcept { return impl_->layout.space_epoch(); }

StateSnapshot StateStore::seal(MutableStateTransaction& transaction)
{
    const auto level_set_group = impl_->layout.level_set_group();
    auto level_set_snapshot = transaction.data_->base->level_set_snapshot;
    if (!vectors_equal(transaction.data_->storage->fields.at(level_set_group.value()),
                       transaction.data_->base->storage->fields.at(level_set_group.value()),
                       impl_->layout.communicator())) {
        level_set_snapshot = reserve_level_set_snapshot_id();
    }

    auto storage = std::shared_ptr<const detail::StateVectorStorage>(std::move(transaction.data_->storage));
    auto candidate = std::make_shared<detail::StateSnapshotData>(detail::StateSnapshotData{
        .stamp = {.space = impl_->layout.space_epoch(),
                  .snapshot = reserve_state_snapshot_id(),
                  .published_epoch = std::nullopt},
        .level_set_snapshot = level_set_snapshot,
        .storage = std::move(storage),
    });
    impl_->snapshots.emplace(candidate->stamp.snapshot.value(), candidate);
    transaction.data_.reset();
    transaction.owner_ = nullptr;
    return StateSnapshot(std::move(candidate));
}

} // namespace rift
