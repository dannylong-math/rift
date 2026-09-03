#pragma once

/**
 * \file
 * \brief Immutable owner-local phase support and collective diagnostics.
 */

#include <cstddef>
#include <cstdint>
#include <deal.II/grid/cell_id.h>
#include <expected>
#include <memory>
#include <optional>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <span>
#include <string>
#include <vector>

namespace rift {

class RiftContext;

/**
 * \brief Request owner-local active cells for one canonical phase.
 */
struct PhaseSupportSpecification {
    /** \brief Canonical phase whose support is requested. */
    PhaseId phase;

    /** \brief Requested locally owned active cells in arbitrary order. */
    std::vector<dealii::CellId> requested_cells;
};

/**
 * \brief Publish one phase's requested and conforming owner-local support.
 *
 * Each closed cell is stored exactly once. Requested cells form the first
 * sorted partition and closure-added cells form the second sorted partition.
 */
class PhaseSupport {
public:
    /** \brief Destroy this phase's partitioned cell storage. */
    ~PhaseSupport() = default;

    /** \brief Copy construction is disabled to avoid deep cell-data copies. */
    PhaseSupport(const PhaseSupport&) = delete;

    /** \brief Copy assignment is disabled to avoid deep cell-data copies. */
    PhaseSupport& operator=(const PhaseSupport&) = delete;

    /** \brief Move construction transfers this phase's complete support. */
    PhaseSupport(PhaseSupport&&) noexcept = default;

    /** \brief Move assignment transfers this phase's complete support. */
    PhaseSupport& operator=(PhaseSupport&&) noexcept = default;

    /** \brief Return the canonical phase described by this support. */
    [[nodiscard]] PhaseId phase_id() const noexcept { return phase_; }

    /** \brief Return the sorted cells supplied by the caller on this rank. */
    [[nodiscard]] std::span<const dealii::CellId> requested_cells() const noexcept;

    /** \brief Return sorted owner-local cells added only for conformity. */
    [[nodiscard]] std::span<const dealii::CellId> closure_added_cells() const noexcept;

    /**
     * \brief Return every closed owner-local cell.
     *
     * This is the requested partition followed by the closure-added partition;
     * the complete span is deterministic but not necessarily globally sorted.
     */
    [[nodiscard]] std::span<const dealii::CellId> closed_cells() const noexcept;

private:
    friend class RiftContext;

    /** \brief Adopt validated partitioned storage produced by the factory. */
    PhaseSupport(PhaseId phase, std::vector<dealii::CellId> cells, std::size_t requested_count) noexcept;

    /** \brief Canonical phase described by this record. */
    PhaseId phase_;

    /** \brief Requested prefix followed by the closure-added suffix. */
    std::vector<dealii::CellId> cells_;

    /** \brief End of the requested prefix in `cells_`. */
    std::size_t requested_count_;
};

/** \brief Classify a recoverable phase-support construction defect. */
enum class PhaseSupportErrorCode : std::uint8_t {
    /** \brief The context has no successfully created phase graph. */
    phase_graph_unavailable,
    /** \brief One rank supplied no mesh snapshot. */
    null_mesh,
    /** \brief One rank invoked the factory with a different dimension. */
    dimension_mismatch,
    /** \brief One rank supplied a different mesh snapshot identity. */
    mesh_snapshot_mismatch,
    /** \brief A specification names no phase in the canonical graph. */
    unknown_phase,
    /** \brief A canonical phase has no specification on one rank. */
    missing_phase_specification,
    /** \brief A canonical phase has more than one specification on one rank. */
    duplicate_phase_specification,
    /** \brief One specification repeats a requested cell. */
    duplicate_requested_cell,
    /** \brief The supplied snapshot does not store a requested cell locally. */
    cell_not_locally_present,
    /** \brief A locally present requested cell is not active. */
    cell_not_active,
    /** \brief An active requested cell is not locally owned. */
    cell_not_locally_owned,
};

/** \brief Describe one rank-local support input error found collectively. */
struct PhaseSupportError {
    /** \brief Machine-readable error classification. */
    PhaseSupportErrorCode code;

    /** \brief World rank whose input caused this error. */
    unsigned int rank;

    /** \brief Associated phase when the error concerns one phase. */
    std::optional<PhaseId> phase;

    /** \brief Associated cell when the error concerns one requested cell. */
    std::optional<dealii::CellId> cell;

    /** \brief Human-readable error description. */
    std::string message;
};

/** \brief Complete deterministic set of collective support errors. */
using PhaseSupportErrors = std::vector<PhaseSupportError>;

/**
 * \brief Own complete immutable owner-local supports for one mesh snapshot.
 *
 * The aggregate has one record per canonical phase and retains the mesh
 * snapshot shared owner. It is movable but not copyable.
 *
 * \tparam dim volume-mesh dimension; only 2 and 3 are supported.
 */
template<int dim>
    requires(dim == 2 || dim == 3)
class PhaseSupportSet {
public:
    /** \brief Destroy all phase support records and release the mesh owner. */
    ~PhaseSupportSet() = default;

    /** \brief Copy construction is disabled to avoid deep cell-data copies. */
    PhaseSupportSet(const PhaseSupportSet&) = delete;

    /** \brief Copy assignment is disabled to avoid deep cell-data copies. */
    PhaseSupportSet& operator=(const PhaseSupportSet&) = delete;

    /** \brief Move construction transfers the complete aggregate. */
    PhaseSupportSet(PhaseSupportSet&&) noexcept = default;

    /** \brief Move assignment transfers the complete aggregate. */
    PhaseSupportSet& operator=(PhaseSupportSet&&) noexcept = default;

    /** \brief Return a borrowed immutable view of the retained mesh snapshot. */
    [[nodiscard]] const MeshSnapshot<dim>& mesh_snapshot() const noexcept;

    /** \brief Return every support in ascending canonical phase-ID order. */
    [[nodiscard]] std::span<const PhaseSupport> supports() const noexcept;

    /**
     * \brief Resolve a canonical phase ID in constant time.
     *
     * \param phase canonical phase identifier.
     * \return immutable support record for that phase.
     * \throws std::out_of_range when `phase` is absent from this set.
     */
    [[nodiscard]] const PhaseSupport& support(PhaseId phase) const;

private:
    friend class RiftContext;

    /** \brief Adopt the validated mesh owner and canonical support records. */
    PhaseSupportSet(std::shared_ptr<const MeshSnapshot<dim>> mesh, std::vector<PhaseSupport> supports) noexcept;

    /** \brief Retain the exact immutable mesh used during closure. */
    std::shared_ptr<const MeshSnapshot<dim>> mesh_;

    /** \brief Per-phase supports in ascending canonical ID order. */
    std::vector<PhaseSupport> supports_;
};

/** \brief Result of collective immutable phase-support construction. */
template<int dim>
    requires(dim == 2 || dim == 3)
using PhaseSupportResult = std::expected<PhaseSupportSet<dim>, PhaseSupportErrors>;

} // namespace rift
