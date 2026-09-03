#pragma once

/**
 * \file
 * \brief Immutable ownership and identity for distributed mesh resources.
 */

#include <cstdint>
#include <deal.II/distributed/tria.h>
#include <deal.II/fe/mapping.h>
#include <expected>
#include <memory>
#include <rift/strong_id.hpp>
#include <string>
#include <vector>

namespace rift {

class RiftContext;

namespace detail {

/** \brief Distinguish mesh snapshot identifiers from other strong IDs. */
struct MeshSnapshotIdTag {};

} // namespace detail

/** \brief Identify one mesh snapshot within a `RiftContext` execution. */
using MeshSnapshotId = StrongId<detail::MeshSnapshotIdTag, std::uint64_t>;

/** \brief Classify a recoverable mesh snapshot construction error. */
enum class MeshSnapshotErrorCode : std::uint8_t {
    /** \brief One rank supplied no distributed triangulation. */
    null_triangulation,
    /** \brief One rank supplied no mapping. */
    null_mapping,
    /** \brief One triangulation does not use `MPI_COMM_WORLD`. */
    communicator_mismatch,
    /** \brief One rank requested a different supported dimension. */
    dimension_mismatch,
};

/** \brief Describe one rank-local input error found collectively. */
struct MeshSnapshotError {
    /** \brief Machine-readable error classification. */
    MeshSnapshotErrorCode code;

    /** \brief World rank whose input caused this error. */
    unsigned int rank;

    /** \brief Human-readable error description. */
    std::string message;

    /** \brief Compare all structured diagnostic fields. */
    friend bool operator==(const MeshSnapshotError&, const MeshSnapshotError&) = default;
};

/** \brief Complete ordered set of mesh snapshot construction errors. */
using MeshSnapshotErrors = std::vector<MeshSnapshotError>;

/**
 * \brief Own one immutable distributed triangulation and mapping.
 *
 * A snapshot owns the resources adopted by
 * `RiftContext::create_mesh_snapshot()`. Public access is const-only, and all
 * handles to the snapshot must be destroyed before their creating context.
 *
 * \tparam dim volume-mesh dimension; only 2 and 3 are supported.
 */
template<int dim>
    requires(dim == 2 || dim == 3)
class MeshSnapshot {
public:
    /** \brief Destroy the owned mapping and triangulation. */
    ~MeshSnapshot() = default;

    /** \brief Copy construction is disabled for the resource owner. */
    MeshSnapshot(const MeshSnapshot&) = delete;

    /** \brief Copy assignment is disabled for the resource owner. */
    MeshSnapshot& operator=(const MeshSnapshot&) = delete;

    /** \brief Move construction is disabled to keep borrowed views stable. */
    MeshSnapshot(MeshSnapshot&&) = delete;

    /** \brief Move assignment is disabled to keep borrowed views stable. */
    MeshSnapshot& operator=(MeshSnapshot&&) = delete;

    /** \brief Return this snapshot's context-local identity. */
    [[nodiscard]] MeshSnapshotId id() const noexcept { return id_; }

    /** \brief Return a const view of the owned distributed triangulation. */
    [[nodiscard]] const dealii::parallel::distributed::Triangulation<dim>& triangulation() const noexcept
    {
        return *triangulation_;
    }

    /** \brief Return a const view of the owned mapping. */
    [[nodiscard]] const dealii::Mapping<dim>& mapping() const noexcept { return *mapping_; }

    /** \brief Return borrowed `MPI_COMM_WORLD` during the context lifetime. */
    [[nodiscard]] MPI_Comm communicator() const noexcept { return MPI_COMM_WORLD; }

private:
    friend class RiftContext;

    /** \brief Adopt validated mesh resources and their assigned identity. */
    MeshSnapshot(MeshSnapshotId id, std::unique_ptr<dealii::parallel::distributed::Triangulation<dim>> triangulation,
                 std::unique_ptr<dealii::Mapping<dim>> mapping) noexcept;

    /** \brief Context-local identity assigned after collective validation. */
    MeshSnapshotId id_;

    /** \brief Exclusively owned triangulation exposed only through a const view. */
    std::unique_ptr<const dealii::parallel::distributed::Triangulation<dim>> triangulation_;

    /** \brief Exclusively owned polymorphic mapping exposed only through a const view. */
    std::unique_ptr<const dealii::Mapping<dim>> mapping_;
};

/** \brief Result of collective immutable mesh snapshot construction. */
template<int dim>
    requires(dim == 2 || dim == 3)
using MeshSnapshotResult = std::expected<std::shared_ptr<const MeshSnapshot<dim>>, MeshSnapshotErrors>;

} // namespace rift
