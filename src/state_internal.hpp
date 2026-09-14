#pragma once

/**
 * \file
 * \brief Private storage shared by state snapshots, stores, and transactions.
 */

#include <cstdint>
#include <deal.II/base/mpi_noncontiguous_partitioner.h>
#include <map>
#include <memory>
#include <mpi.h>
#include <rift/state_store.hpp>
#include <utility>
#include <vector>

namespace rift {

/** \brief Private compact arrays and native-index communication metadata. */
struct PhaseLabelMetadata::Impl {
    /** \brief Component-filtered authoritative native DoF indices. */
    dealii::IndexSet locally_owned_dofs;
    /** \brief Component-filtered nonowned native DoF indices. */
    dealii::IndexSet ghost_dofs;
    /** \brief Immutable sparse owner-to-ghost exchange plan. */
    std::shared_ptr<const dealii::Utilities::MPI::NoncontiguousPartitioner> partitioner;
    /** \brief Encoded values in `locally_owned_dofs` order. */
    std::vector<std::uint32_t> owned_values;
    /** \brief Encoded values in `ghost_dofs` order. */
    std::vector<std::uint32_t> ghost_values;
    /** \brief Number of canonical phases valid in this block. */
    std::uint64_t phase_count = 0;
};

namespace detail {

/** \brief Own every category-separated value block for one complete state. */
template<int dim>
    requires(dim == 2 || dim == 3)
struct StateStorage {
    /** \brief Native ghost-capable continuous-vector representation. */
    using Vector = dealii::LinearAlgebra::distributed::Vector<double>;

    /** \brief Construct empty category storage. */
    StateStorage() = default;
    /** \brief Deep-copy all candidate values and vector partitioners. */
    StateStorage(const StateStorage&) = default;
    /** \brief Deep-copy-assign all candidate values and vector partitioners. */
    StateStorage& operator=(const StateStorage&) = default;
    /** \brief Move all category storage without changing its values. */
    StateStorage(StateStorage&&) noexcept = default;
    /** \brief Move-assign all category storage. */
    StateStorage& operator=(StateStorage&&) noexcept = default;

    /** \brief Support-restricted continuous vectors in canonical order. */
    std::vector<Vector> phase_support_fields;
    /** \brief Primitive continuous geometry vectors in canonical order. */
    std::vector<Vector> geometry_fields;
    /** \brief Compact phase-label blocks in canonical order. */
    std::vector<PhaseLabelMetadata> discrete_geometry_metadata;
    /** \brief Complete canonically ordered replicated regional values. */
    std::vector<double> regional_values;
};

/** \brief Store one indexed snapshot and its optional pin role. */
template<int dim>
    requires(dim == 2 || dim == 3)
struct RetainedSnapshot {
    /** \brief Shared immutable state bundle. */
    std::shared_ptr<const StateSnapshot<dim>> snapshot;
    /** \brief Whether explicit retention capacity protects this identity. */
    bool pinned = false;
};

/** \brief Stable publication authority shared weakly with transactions. */
template<int dim>
    requires(dim == 2 || dim == 3)
struct StateStoreControl {
    /** \brief Non-owning context identity used only for provenance. */
    const RiftContext* creator_context = nullptr;
    /** \brief Borrowed world communicator valid during the context lifetime. */
    MPI_Comm communicator = MPI_COMM_NULL;
    /** \brief Context-local store identity. */
    StateStoreId id = StateStoreId::from_index(0);
    /** \brief Finalized immutable state layout. */
    std::shared_ptr<const SpaceSnapshot<dim>> space;
    /** \brief Configured maximum explicit pins. */
    RetentionPolicy retention;

    /** \brief Current accepted root. */
    std::shared_ptr<const StateSnapshot<dim>> accepted;
    /** \brief Immediately preceding accepted root. */
    std::shared_ptr<const StateSnapshot<dim>> previous;
    /** \brief Most recently sealed unpinned-candidate role. */
    std::shared_ptr<const StateSnapshot<dim>> transient;
    /** \brief Every snapshot still eligible for store operations. */
    std::map<std::uint64_t, RetainedSnapshot<dim>> retained;
    /** \brief Number of records carrying the orthogonal pin role. */
    std::size_t pinned_count = 0;

    /** \brief Next transaction identity candidate. */
    std::uint64_t next_transaction_id = 0;
    /** \brief Whether the transaction sequence has no remaining value. */
    bool transaction_ids_exhausted = false;
    /** \brief Next nonroot snapshot identity candidate. */
    std::uint64_t next_snapshot_id = 1;
    /** \brief Whether the snapshot sequence has no remaining value. */
    bool snapshot_ids_exhausted = false;
    /** \brief Next changed-geometry revision candidate. */
    std::uint64_t next_geometry_revision = 1;
    /** \brief Whether the geometry-revision sequence has no remaining value. */
    bool geometry_revisions_exhausted = false;
};

} // namespace detail
} // namespace rift
