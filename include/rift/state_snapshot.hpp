#pragma once

/**
 * \file
 * \brief Immutable state snapshots and exact state identities.
 */

#include <cstdint>
#include <deal.II/base/index_set.h>
#include <deal.II/base/types.h>
#include <deal.II/lac/la_parallel_vector.h>
#include <memory>
#include <optional>
#include <rift/space_snapshot.hpp>
#include <rift/strong_id.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace rift {

template<int dim>
    requires(dim == 2 || dim == 3)
class StateStore;
template<int dim>
    requires(dim == 2 || dim == 3)
class StateTransaction;

namespace detail {

/** \brief Distinguish state-store identifiers. */
struct StateStoreIdTag {};
/** \brief Distinguish immutable state-snapshot identifiers. */
struct StateSnapshotIdTag {};
/** \brief Distinguish geometry revisions. */
struct GeometryRevisionTag {};
/** \brief Distinguish transaction identifiers. */
struct StateTransactionIdTag {};

template<int dim>
    requires(dim == 2 || dim == 3)
struct StateStorage;

} // namespace detail

/** \brief Identify one state store created by a `RiftContext`. */
using StateStoreId = StrongId<detail::StateStoreIdTag, std::uint64_t>;
/** \brief Identify one immutable snapshot within a state store. */
using StateSnapshotId = StrongId<detail::StateSnapshotIdTag, std::uint64_t>;
/** \brief Identify one primitive-geometry revision within a state store. */
using GeometryRevision = StrongId<detail::GeometryRevisionTag, std::uint64_t>;
/** \brief Identify one transaction within a state store. */
using StateTransactionId = StrongId<detail::StateTransactionIdTag, std::uint64_t>;

/** \brief Bind a snapshot identity to the state store that allocated it. */
struct StateSnapshotReference {
    /** \brief Context-local state-store identity. */
    StateStoreId store_id;
    /** \brief Store-local immutable snapshot identity. */
    StateSnapshotId snapshot_id;

    /** \brief Compare complete snapshot provenance. */
    friend bool operator==(const StateSnapshotReference&, const StateSnapshotReference&) = default;
};

/** \brief Describe the immutable identity and lineage of one complete state. */
struct StateSnapshotStamp {
    /** \brief Finalized space that interprets all state indices. */
    SpaceEpoch space_epoch = SpaceEpoch::from_index(0);
    /** \brief Store that allocated this snapshot. */
    StateStoreId store_id = StateStoreId::from_index(0);
    /** \brief Immutable state identity within the store. */
    StateSnapshotId snapshot_id = StateSnapshotId::from_index(0);
    /** \brief Transaction base, absent only for the initial root. */
    std::optional<StateSnapshotId> base_snapshot_id;
    /** \brief Exact primitive-geometry revision. */
    GeometryRevision geometry_revision = GeometryRevision::from_index(0);

    /** \brief Compare the complete immutable stamp. */
    friend bool operator==(const StateSnapshotStamp&, const StateSnapshotStamp&) = default;
};

/** \brief Classify a recoverable state transition or lookup failure. */
enum class StateErrorCode : std::uint8_t {
    /** \brief The store needed by a transaction no longer exists. */
    expired_store,
    /** \brief A collective seal targeted an inactive transaction. */
    inactive_transaction,
    /** \brief Ranks entered different state operations or identities. */
    collective_operation_mismatch,
    /** \brief No further transaction identity can be represented. */
    transaction_id_exhausted,
    /** \brief No further immutable snapshot identity can be represented. */
    state_snapshot_id_exhausted,
    /** \brief Changed geometry cannot receive another revision. */
    geometry_revision_exhausted,
    /** \brief A snapshot reference names another state store. */
    foreign_snapshot,
    /** \brief The store no longer indexes the requested snapshot. */
    snapshot_not_retained,
    /** \brief The requested transition does not apply to this snapshot role. */
    invalid_snapshot_transition,
    /** \brief An explicitly pinned snapshot cannot be discarded. */
    snapshot_pinned,
    /** \brief The configured pin capacity is already full. */
    pin_capacity_exceeded,
    /** \brief The candidate's accepted root is no longer current. */
    stale_candidate,
    /** \brief This rank is not authoritative for the regional entry. */
    not_regional_owner,
};

/** \brief Describe one state error with optional snapshot provenance. */
struct StateError {
    /** \brief Machine-readable failure classification. */
    StateErrorCode code;
    /** \brief World rank that observed or supplied the defect. */
    unsigned int rank;
    /** \brief Affected snapshot when the operation has one. */
    std::optional<StateSnapshotReference> snapshot;
    /** \brief Human-readable diagnostic. */
    std::string message;
};

/** \brief Complete deterministic collective state-error set. */
using StateErrors = std::vector<StateError>;

/**
 * \brief Store compact phase labels for one geometry-field component.
 *
 * Values are addressed by the geometry field's native global DoF index. The
 * hidden 32-bit representation has one distinct unassigned value, while the
 * public API uses `std::optional<PhaseId>`.
 */
class PhaseLabelMetadata {
public:
    /** \brief Destroy compact arrays and their shared communication plan. */
    ~PhaseLabelMetadata();
    /** \brief Deep-copy values while sharing immutable communication metadata. */
    PhaseLabelMetadata(const PhaseLabelMetadata& other);
    /** \brief Deep-copy assignment of values and immutable layout metadata. */
    PhaseLabelMetadata& operator=(const PhaseLabelMetadata& other);
    /** \brief Move compact storage. */
    PhaseLabelMetadata(PhaseLabelMetadata&&) noexcept;
    /** \brief Move-assign compact storage. */
    PhaseLabelMetadata& operator=(PhaseLabelMetadata&&) noexcept;

    /** \brief Return native DoF indices whose values are authoritative here. */
    [[nodiscard]] const dealii::IndexSet& locally_owned_dofs() const noexcept;
    /** \brief Return native DoF indices imported from other ranks. */
    [[nodiscard]] const dealii::IndexSet& ghost_dofs() const noexcept;
    /** \brief Report whether this compact block stores the native DoF index. */
    [[nodiscard]] bool contains(dealii::types::global_dof_index dof) const;
    /** \brief Report whether this rank owns the native DoF index. */
    [[nodiscard]] bool locally_owns(dealii::types::global_dof_index dof) const;

    /**
     * \brief Read an assigned phase or the explicit unassigned state.
     * \throws std::out_of_range when `dof` is not locally relevant.
     */
    [[nodiscard]] std::optional<PhaseId> label(dealii::types::global_dof_index dof) const;

private:
    template<int dim>
        requires(dim == 2 || dim == 3)
    friend class StateStore;
    template<int dim>
        requires(dim == 2 || dim == 3)
    friend class StateTransaction;
    friend class MutablePhaseLabelMetadataView;

    struct Impl;
    /** \brief Adopt fully constructed compact metadata storage. */
    explicit PhaseLabelMetadata(std::unique_ptr<Impl> implementation) noexcept;
    /** \brief Import authoritative values into retained ghost entries. */
    void update_ghost_values();
    /** \brief Borrow authoritative compact codes for exact comparisons. */
    [[nodiscard]] std::span<const std::uint32_t> owned_codes() const noexcept;

    /** \brief Opaque compact values, index maps, and communication pattern. */
    std::unique_ptr<Impl> implementation_;
};

/** \brief Provide owned-only mutation of one compact phase-label block. */
class MutablePhaseLabelMetadataView {
public:
    /** \brief Return the native DoFs writable through this view. */
    [[nodiscard]] const dealii::IndexSet& locally_owned_dofs() const noexcept;
    /** \brief Read one locally relevant label. */
    [[nodiscard]] std::optional<PhaseId> label(dealii::types::global_dof_index dof) const;
    /**
     * \brief Assign a phase or the unassigned state at one owned native DoF.
     * \throws std::out_of_range when `dof` is not locally owned.
     */
    void set_label(dealii::types::global_dof_index dof, std::optional<PhaseId> phase);

private:
    template<int dim>
        requires(dim == 2 || dim == 3)
    friend class StateTransaction;
    /** \brief Borrow an active transaction's owning metadata block. */
    explicit MutablePhaseLabelMetadataView(PhaseLabelMetadata& metadata) noexcept;

    /** \brief Borrowed candidate block owned by an active transaction. */
    PhaseLabelMetadata* metadata_;
};

/** \brief Publish one complete immutable state vector bundle. */
template<int dim>
    requires(dim == 2 || dim == 3)
class StateSnapshot {
public:
    /** \brief Destroy state storage after all borrowed views have expired. */
    ~StateSnapshot();
    StateSnapshot(const StateSnapshot&) = delete;
    StateSnapshot& operator=(const StateSnapshot&) = delete;
    StateSnapshot(StateSnapshot&&) = delete;
    StateSnapshot& operator=(StateSnapshot&&) = delete;

    /** \brief Return immutable state identity, lineage, and geometry revision. */
    [[nodiscard]] const StateSnapshotStamp& stamp() const noexcept;
    /** \brief Return the compact reference accepted by store operations. */
    [[nodiscard]] StateSnapshotReference reference() const noexcept;
    /** \brief Return the finalized space that interprets this state. */
    [[nodiscard]] const SpaceSnapshot<dim>& space() const noexcept;

    /** \brief Read one support-restricted continuous field vector. */
    [[nodiscard]] const dealii::LinearAlgebra::distributed::Vector<double>&
    phase_support_field(PhaseSupportFieldReference reference) const;
    /** \brief Read one continuous geometry-field vector. */
    [[nodiscard]] const dealii::LinearAlgebra::distributed::Vector<double>&
    geometry_field(GeometryFieldReference reference) const;
    /** \brief Read one compact discrete geometry-metadata block. */
    [[nodiscard]] const PhaseLabelMetadata&
    discrete_geometry_metadata(DiscreteGeometryMetadataReference reference) const;
    /** \brief Read one locally replicated nonspatial scalar by value. */
    [[nodiscard]] double regional(RegionalEntryReference reference) const;

private:
    friend class StateStore<dim>;
    friend class StateTransaction<dim>;

    /** \brief Adopt a finalized space, immutable stamp, and complete storage. */
    StateSnapshot(std::shared_ptr<const SpaceSnapshot<dim>> space, StateSnapshotStamp stamp,
                  std::unique_ptr<detail::StateStorage<dim>> storage) noexcept;

    /** \brief Retain the exact finalized layout used by every block. */
    std::shared_ptr<const SpaceSnapshot<dim>> space_;
    /** \brief Immutable composite state identity. */
    StateSnapshotStamp stamp_;
    /** \brief Opaque category-separated state storage. */
    std::unique_ptr<detail::StateStorage<dim>> storage_;
};

} // namespace rift
