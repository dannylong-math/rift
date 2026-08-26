#pragma once

/**
 * \file
 * \brief Immutable ownership and provenance for deal.II triangulations.
 */

#include <cstdint>
#include <deal.II/distributed/tria.h>
#include <deal.II/distributed/tria_base.h>
#include <deal.II/grid/tria.h>
#include <expected>
#include <memory>
#include <mpi.h>
#include <new>
#include <rift/run_configuration.hpp>
#include <rift/strong_id.hpp>
#include <string>
#include <utility>

namespace rift {

/**
 * \defgroup mesh_snapshot Immutable mesh snapshots
 * \brief Run-owned triangulations with authoritative communicators and stable provenance.
 */

namespace detail {

/**
 * \brief Distinguish mesh-snapshot identities from every other strong identifier.
 *
 * \code{.cpp}
 * using Id = rift::StrongId<rift::detail::MeshSnapshotIdTag, std::uint64_t>;
 * const Id id = Id::from_index(0);
 * \endcode
 *
 * \ingroup mesh_snapshot
 */
struct MeshSnapshotIdTag {};

/** \brief Private run control retained by every immutable mesh snapshot. */
class RunConfigurationControl;

/** \brief Own the duplicated communicator used by a reconstructed distributed mesh. */
class MeshCommunicatorControl;

} // namespace detail

/** \brief Identity never reused by another mesh snapshot in one MPI execution. */
using MeshSnapshotId = StrongId<detail::MeshSnapshotIdTag, std::uint64_t>;

/**
 * \brief Pair a mesh identity with the run that authorized its construction.
 *
 * Use the complete pair when validating compatibility. A raw mesh number does
 * not establish that another object belongs to the same run.
 *
 * \code{.cpp}
 * const rift::MeshSnapshotProvenance provenance{
 *     .run = run.id(),
 *     .mesh = snapshot->id(),
 * };
 * if (snapshot->provenance() == provenance) {
 *     std::cout << "matching mesh provenance\n";
 * }
 * \endcode
 */
struct MeshSnapshotProvenance {
    /** \brief Run identity that authorized the snapshot. */
    RunConfigurationId run;
    /** \brief Process-execution-wide mesh identity. */
    MeshSnapshotId mesh;

    /** \brief Compare provenance only when both the run and mesh identities match. */
    friend bool operator==(const MeshSnapshotProvenance& left, const MeshSnapshotProvenance& right) noexcept
    {
        return left.run == right.run && left.mesh == right.mesh;
    }
};

/** \brief Classify a recoverable mesh-snapshot construction failure. */
enum class MeshSnapshotErrorCode : std::uint8_t {
    /** \brief MPI has not been initialized. */
    mpi_not_initialized,
    /** \brief MPI has already been finalized. */
    mpi_finalized,
    /** \brief At least one rank supplied no triangulation. */
    null_triangulation,
    /** \brief At least one triangulation uses an intercommunicator. */
    intercommunicator_not_supported,
    /** \brief At least one mesh communicator member is outside the current MPI world. */
    communicator_not_world_derived,
    /** \brief The mesh communicator has the run group in a different rank order. */
    communicator_reordered,
    /** \brief The mesh and run communicators have different process groups. */
    communicator_mismatch,
    /** \brief A parallel triangulation kind cannot yet be retained without changing semantics. */
    unsupported_triangulation_kind,
    /** \brief A distributed triangulation is empty and cannot be reconstructed safely. */
    empty_distributed_triangulation,
    /** \brief The finite process-global mesh identity space is exhausted. */
    id_space_exhausted,
};

/**
 * \brief Report one deterministic mesh-snapshot construction failure.
 *
 * \code{.cpp}
 * auto result = rift::make_mesh_snapshot(run, std::move(triangulation));
 * if (!result) {
 *     std::cerr << result.error().message << '\n';
 * }
 * \endcode
 */
struct MeshSnapshotError {
    /** \brief Machine-readable failure classification. */
    MeshSnapshotErrorCode code;
    /** \brief Deterministic human-readable diagnostic. */
    std::string message;
};

template<int dim> class MeshSnapshot;

namespace detail {

/**
 * \brief Carry validated run, communicator, and provenance into immutable allocation.
 *
 * \code{.cpp}
 * auto preparation = rift::detail::prepare_mesh_snapshot(run_control,
 *                                                         mesh_communicator);
 * if (!preparation) {
 *     return std::unexpected(preparation.error());
 * }
 * \endcode
 */
struct MeshSnapshotPreparation {
    /** \brief Shared control retaining run identity and communicator lifetime. */
    std::shared_ptr<const RunConfigurationControl> run_control;
    /** \brief Communicator borrowed from the owned triangulation. */
    MPI_Comm communicator;
    /** \brief Run and globally unique mesh identity. */
    MeshSnapshotProvenance provenance;
};

/**
 * \brief Classify storage requirements before communicator-stable reconstruction.
 *
 * Lower values have deterministic collective precedence. Serial
 * `dealii::Triangulation` and nonempty
 * `dealii::parallel::distributed::Triangulation` are supported. Other
 * parallel triangulation kinds are rejected until Rift can reconstruct their
 * exact semantics on an owned communicator.
 */
enum class MeshStorageClassification : std::uint8_t {
    /** \brief A parallel triangulation kind other than the supported distributed kind. */
    unsupported = 0,
    /** \brief A supported distributed triangulation has no active cells. */
    empty_distributed = 1,
    /** \brief The mesh can be retained without changing its triangulation kind. */
    valid = 2,
};

/**
 * \brief Retain one duplicated communicator and its borrowed handle.
 *
 * \code{.cpp}
 * const auto owned = rift::detail::own_mesh_communicator(run_control,
 *                                                        source_communicator);
 * \endcode
 */
struct RetainedMeshCommunicator {
    /** \brief Shared RAII control for the duplicated handle. */
    std::shared_ptr<const MeshCommunicatorControl> control;
    /** \brief Handle owned by `control`. */
    MPI_Comm communicator = MPI_COMM_NULL;
};

/**
 * \brief Hold communicator-stable triangulation storage before snapshot allocation.
 *
 * \code{.cpp}
 * auto storage = rift::detail::stabilize_mesh_storage(
 *     run_control, std::move(triangulation), source_communicator);
 * \endcode
 *
 * \tparam dim triangulation dimension.
 */
template<int dim> struct MeshSnapshotStorage {
    /** \brief Owned communicator control, null for a serial SELF mesh. */
    std::shared_ptr<const MeshCommunicatorControl> communicator_control;
    /** \brief Consumed serial mesh or reconstructed distributed mesh. */
    std::unique_ptr<dealii::Triangulation<dim>> triangulation;
    /** \brief Authoritative communicator used by `triangulation`. */
    MPI_Comm communicator = MPI_COMM_NULL;
};

template<int dim> struct MeshSnapshotFactory;
template<int dim> struct MeshSnapshotAccess;

/**
 * \brief Collectively retain the run only when every rank supplied a mesh.
 * \param run run defining the collective call.
 * \param mesh_present whether this rank received a non-null triangulation.
 * \return retained run control, or the communicator-consistent first validation error.
 */
[[nodiscard]] std::expected<std::shared_ptr<const RunConfigurationControl>, MeshSnapshotError>
retain_mesh_snapshot_run_context(const RunConfiguration& run, bool mesh_present);

/**
 * \brief Validate a mesh communicator and allocate its communicator-consistent identity.
 * \param run_control retained run control from collective null validation.
 * \param mesh_communicator communicator derived from the triangulation.
 * \param storage_classification locally inspected triangulation kind and empty state.
 * \return validated allocation preparation, or a deterministic collective error.
 */
[[nodiscard]] std::expected<MeshSnapshotPreparation, MeshSnapshotError>
prepare_mesh_snapshot(std::shared_ptr<const RunConfigurationControl> run_control, MPI_Comm mesh_communicator,
                      MeshStorageClassification storage_classification);

/**
 * \brief Route unsafe local allocation failure through the retained fatal handler.
 * \param run_control run whose participants cannot safely rejoin independently.
 */
[[noreturn]] void abort_mesh_snapshot_allocation(const std::shared_ptr<const RunConfigurationControl>& run_control);

/**
 * \brief Route an unsafe post-validation reconstruction exception through the fatal handler.
 * \param run_control run whose ranks may be in unmatched deal.II collectives.
 * \param status `MPI_ERR_NO_MEM` for allocation or `MPI_ERR_OTHER` for another exception.
 */
[[noreturn]] void abort_mesh_snapshot_reconstruction(const std::shared_ptr<const RunConfigurationControl>& run_control,
                                                     int status);

/**
 * \brief Duplicate a validated distributed-mesh communicator and retain it.
 * \param run_control retained fatal policy and run communicator.
 * \param source communicator derived from the consumed triangulation.
 * \return owned duplicate and borrowed handle.
 */
[[nodiscard]] RetainedMeshCommunicator
own_mesh_communicator(const std::shared_ptr<const RunConfigurationControl>& run_control, MPI_Comm source);

/**
 * \brief Prevent rank-local fatal unwinding from entering collective communicator release.
 * \param control owned communicator control, or null for serial storage.
 *
 * The duplicated handle is deliberately left for MPI teardown. Call this only
 * immediately before the retained fatal handler, when ranks cannot safely
 * rejoin ordinary execution or collectively release the communicator.
 */
void abandon_mesh_communicator(const std::shared_ptr<const MeshCommunicatorControl>& control) noexcept;

/**
 * \brief Signature for private immutable-snapshot allocation injection.
 * \tparam dim triangulation dimension.
 */
template<int dim>
using MeshSnapshotAllocator = std::shared_ptr<const MeshSnapshot<dim>> (*)(
    std::shared_ptr<const RunConfigurationControl>, std::shared_ptr<const MeshCommunicatorControl>,
    std::unique_ptr<dealii::Triangulation<dim>>, MeshSnapshotProvenance, MPI_Comm);

/**
 * \brief Signature for private distributed-mesh reconstruction injection.
 * \tparam dim triangulation dimension.
 */
template<int dim>
using DistributedMeshRebuilder = MeshSnapshotStorage<dim> (*)(const std::shared_ptr<const RunConfigurationControl>&,
                                                              std::unique_ptr<dealii::Triangulation<dim>>, MPI_Comm);

/**
 * \brief Signature for constructing and copying distributed storage after communicator ownership.
 * \tparam dim triangulation dimension.
 */
template<int dim>
using DistributedTriangulationBuilder =
    std::unique_ptr<dealii::Triangulation<dim>> (*)(MPI_Comm, const dealii::Triangulation<dim>&);

/**
 * \brief Construct one distributed triangulation and copy the consumed source mesh.
 * \tparam dim triangulation dimension.
 * \param communicator owned communicator used by the replacement triangulation.
 * \param source validated nonempty distributed source triangulation.
 * \return reconstructed distributed triangulation.
 * \throws std::bad_alloc or a deal.II copy exception for the fatal boundary.
 */
template<int dim>
[[nodiscard]] std::unique_ptr<dealii::Triangulation<dim>>
build_distributed_triangulation(const MPI_Comm communicator, const dealii::Triangulation<dim>& source)
{
    std::unique_ptr<dealii::Triangulation<dim>> stable =
        std::make_unique<dealii::parallel::distributed::Triangulation<dim>>(communicator);
    stable->copy_triangulation(source);
    return stable;
}

/**
 * \brief Complete distributed reconstruction after communicator ownership.
 * \tparam dim triangulation dimension.
 * \param run_control retained fatal policy.
 * \param triangulation consumed nonempty distributed source.
 * \param retained already duplicated and retained communicator.
 * \param build injected replacement allocation and copy operation.
 * \return reconstructed storage retaining the owned communicator.
 *
 * A rank-local allocation or copy exception abandons ordinary communicator
 * cleanup before invoking the fatal handler. No rank may enter a collective
 * `MPI_Comm_free` while peers remain inside deal.II reconstruction.
 */
template<int dim>
[[nodiscard]] MeshSnapshotStorage<dim>
rebuild_distributed_mesh_with_builder(const std::shared_ptr<const RunConfigurationControl>& run_control,
                                      std::unique_ptr<dealii::Triangulation<dim>> triangulation,
                                      RetainedMeshCommunicator retained,
                                      const DistributedTriangulationBuilder<dim> build)
{
    try {
        auto stable = build(retained.communicator, *triangulation);
        return {.communicator_control = std::move(retained.control),
                .triangulation = std::move(stable),
                .communicator = retained.communicator};
    }
    catch (const std::bad_alloc&) {
        abandon_mesh_communicator(retained.control);
        abort_mesh_snapshot_reconstruction(run_control, MPI_ERR_NO_MEM);
    }
    catch (...) {
        abandon_mesh_communicator(retained.control);
        abort_mesh_snapshot_reconstruction(run_control, MPI_ERR_OTHER);
    }
}

/**
 * \brief Reconstruct a supported distributed mesh on an owned communicator.
 * \tparam dim triangulation dimension.
 * \param run_control retained fatal policy.
 * \param triangulation nonempty supported distributed triangulation.
 * \param source_communicator validated borrowed communicator.
 * \return reconstructed storage retaining the owned communicator.
 * \throws std::bad_alloc or another reconstruction exception for the outer fatal boundary.
 */
template<int dim>
[[nodiscard]] MeshSnapshotStorage<dim>
rebuild_distributed_mesh(const std::shared_ptr<const RunConfigurationControl>& run_control,
                         std::unique_ptr<dealii::Triangulation<dim>> triangulation, const MPI_Comm source_communicator)
{
    auto retained = own_mesh_communicator(run_control, source_communicator);
    return rebuild_distributed_mesh_with_builder(run_control, std::move(triangulation), std::move(retained),
                                                 build_distributed_triangulation<dim>);
}

/**
 * \brief Stabilize mesh storage behind an injected reconstruction operation.
 * \tparam dim triangulation dimension.
 * \param run_control retained fatal policy.
 * \param triangulation consumed validated triangulation.
 * \param source_communicator communicator derived from `triangulation`.
 * \param rebuild injected distributed reconstruction operation.
 * \return stable serial or distributed storage.
 */
template<int dim>
[[nodiscard]] MeshSnapshotStorage<dim>
stabilize_mesh_storage_with_rebuilder(const std::shared_ptr<const RunConfigurationControl>& run_control,
                                      std::unique_ptr<dealii::Triangulation<dim>> triangulation,
                                      const MPI_Comm source_communicator, const DistributedMeshRebuilder<dim> rebuild)
{
    if (dynamic_cast<dealii::parallel::distributed::Triangulation<dim>*>(triangulation.get()) == nullptr) {
        return {
            .communicator_control = nullptr, .triangulation = std::move(triangulation), .communicator = MPI_COMM_SELF};
    }

    try {
        return rebuild(run_control, std::move(triangulation), source_communicator);
    }
    catch (const std::bad_alloc&) {
        abort_mesh_snapshot_reconstruction(run_control, MPI_ERR_NO_MEM);
    }
    catch (...) {
        abort_mesh_snapshot_reconstruction(run_control, MPI_ERR_OTHER);
    }
}

/**
 * \brief Make a consumed distributed triangulation independent of its caller communicator.
 *
 * deal.II 9.8 distributed triangulations borrow their constructor
 * communicator. Rift therefore duplicates that communicator and copies the
 * consumed mesh into a distributed triangulation constructed on the owned
 * duplicate. Serial triangulations already use permanent `MPI_COMM_SELF`.
 *
 * \tparam dim triangulation dimension.
 * \param run_control retained fatal policy.
 * \param triangulation consumed validated triangulation.
 * \param source_communicator communicator derived from `triangulation`.
 * \return communicator-stable storage for immutable allocation.
 */
template<int dim>
[[nodiscard]] MeshSnapshotStorage<dim>
stabilize_mesh_storage(const std::shared_ptr<const RunConfigurationControl>& run_control,
                       std::unique_ptr<dealii::Triangulation<dim>> triangulation, const MPI_Comm source_communicator)
{
    return stabilize_mesh_storage_with_rebuilder(run_control, std::move(triangulation), source_communicator,
                                                 rebuild_distributed_mesh<dim>);
}

/**
 * \brief Classify one consumed triangulation without performing mesh collectives.
 * \tparam dim triangulation dimension.
 * \param triangulation non-null transferred triangulation.
 * \return local storage classification for run-wide agreement.
 */
template<int dim>
[[nodiscard]] MeshStorageClassification classify_mesh_storage(const dealii::Triangulation<dim>& triangulation) noexcept
{
    if (const auto* distributed =
            dynamic_cast<const dealii::parallel::distributed::Triangulation<dim>*>(&triangulation)) {
        return distributed->n_active_cells() == 0U ? MeshStorageClassification::empty_distributed
                                                   : MeshStorageClassification::valid;
    }
    if (dynamic_cast<const dealii::parallel::TriangulationBase<dim>*>(&triangulation) != nullptr) {
        return MeshStorageClassification::unsupported;
    }
    return MeshStorageClassification::valid;
}

} // namespace detail

/**
 * \brief Own one triangulation immutably together with run and mesh provenance.
 *
 * \par When to use
 * Transfer a completed serial or distributed triangulation into a snapshot
 * before constructing spaces, layouts, or state. Later objects retain the
 * shared snapshot rather than borrowing application-owned mutable mesh data.
 *
 * \par Typical use
 * \code{.cpp}
 * int main(int argc, char **argv) {
 *     MPI_Init(&argc, &argv);
 *     {
 *         auto run = rift::RunConfiguration::create(MPI_COMM_SELF).value();
 *         auto mesh = std::make_unique<dealii::Triangulation<2>>();
 *         dealii::GridGenerator::hyper_cube(*mesh);
 *
 *         auto result = rift::make_mesh_snapshot(run, std::move(mesh));
 *         if (!result) {
 *             std::cerr << result.error().message << '\n';
 *             MPI_Abort(MPI_COMM_SELF, 1);
 *         }
 *         const auto snapshot = *result;
 *         std::cout << snapshot->triangulation().n_active_cells() << '\n';
 *     }
 *     MPI_Finalize();
 * }
 * \endcode
 *
 * \par Important behavior
 * The run control is declared before the triangulation so destruction releases
 * the triangulation and its communicator first. Public access is const-only;
 * only the private controlled gateway may mutate the mesh while constructing
 * a later immutable generation.
 *
 * \tparam dim triangulation dimension.
 */
template<int dim> class MeshSnapshot {
public:
    /** \brief Read this snapshot's never-reused mesh identity. */
    [[nodiscard]] MeshSnapshotId id() const noexcept { return provenance_.mesh; }

    /** \brief Read the complete run and mesh provenance pair. */
    [[nodiscard]] MeshSnapshotProvenance provenance() const noexcept { return provenance_; }

    /** \brief Borrow the authoritative communicator owned by the triangulation. */
    [[nodiscard]] MPI_Comm communicator() const noexcept { return communicator_; }

    /** \brief Inspect the immutable triangulation owned by this snapshot. */
    [[nodiscard]] const dealii::Triangulation<dim>& triangulation() const noexcept { return *triangulation_; }

private:
    /** \brief Allow validated immutable allocation. */
    friend struct detail::MeshSnapshotFactory<dim>;
    /** \brief Allow controlled internal mutation during later generation construction. */
    friend struct detail::MeshSnapshotAccess<dim>;

    /**
     * \brief Adopt validated run control and exclusive triangulation ownership.
     * \param run_control retained run lifetime and fatal policy.
     * \param communicator_control owned distributed-mesh communicator, or null for serial.
     * \param triangulation exclusively owned mesh.
     * \param provenance run and mesh identity.
     * \param communicator communicator borrowed from `triangulation`.
     */
    MeshSnapshot(std::shared_ptr<const detail::RunConfigurationControl> run_control,
                 std::shared_ptr<const detail::MeshCommunicatorControl> communicator_control,
                 std::unique_ptr<dealii::Triangulation<dim>> triangulation, MeshSnapshotProvenance provenance,
                 MPI_Comm communicator) noexcept :
        run_control_(std::move(run_control)),
        communicator_control_(std::move(communicator_control)),
        triangulation_(std::move(triangulation)),
        provenance_(provenance),
        communicator_(communicator)
    {
    }

    /** \brief Retained first so it outlives triangulation destruction. */
    std::shared_ptr<const detail::RunConfigurationControl> run_control_;
    /** \brief Owned distributed-mesh communicator destroyed after the triangulation. */
    std::shared_ptr<const detail::MeshCommunicatorControl> communicator_control_;
    /** \brief Exclusively owned mesh destroyed before `run_control_`. */
    std::unique_ptr<dealii::Triangulation<dim>> triangulation_;
    /** \brief Immutable compatibility identity. */
    MeshSnapshotProvenance provenance_;
    /** \brief Borrowed handle whose lifetime is enclosed by `triangulation_`. */
    MPI_Comm communicator_;
};

namespace detail {

/**
 * \brief Allocate an immutable snapshot after collective validation.
 *
 * \code{.cpp}
 * auto snapshot = rift::detail::MeshSnapshotFactory<2>::create(
 *     control, communicator_control, std::move(mesh), provenance, communicator);
 * \endcode
 */
template<int dim> struct MeshSnapshotFactory {
    /**
     * \brief Transfer validated ownership into shared immutable storage.
     * \param run_control retained run control.
     * \param communicator_control owned distributed communicator, or null for serial storage.
     * \param triangulation exclusively owned triangulation.
     * \param provenance allocated provenance.
     * \param communicator communicator borrowed from the triangulation.
     * \return shared immutable snapshot.
     * \throws std::bad_alloc when snapshot or shared-control allocation fails.
     */
    [[nodiscard]] static std::shared_ptr<const MeshSnapshot<dim>>
    create(std::shared_ptr<const RunConfigurationControl> run_control,
           std::shared_ptr<const MeshCommunicatorControl> communicator_control,
           std::unique_ptr<dealii::Triangulation<dim>> triangulation, const MeshSnapshotProvenance provenance,
           const MPI_Comm communicator)
    {
        return std::shared_ptr<const MeshSnapshot<dim>>(
            new MeshSnapshot<dim>(std::move(run_control), std::move(communicator_control), std::move(triangulation),
                                  provenance, communicator));
    }
};

/**
 * \brief Give trusted later factories controlled mutable access to an owned mesh.
 *
 * \code{.cpp}
 * auto &mesh = rift::detail::MeshSnapshotAccess<2>::mutable_triangulation(*snapshot);
 * \endcode
 */
template<int dim> struct MeshSnapshotAccess {
    /** \brief Borrow mutable mesh storage while building a replacement immutable generation. */
    [[nodiscard]] static dealii::Triangulation<dim>& mutable_triangulation(const MeshSnapshot<dim>& snapshot) noexcept
    {
        return *snapshot.triangulation_;
    }

    /** \brief Retain the run control for a later run-owned object. */
    [[nodiscard]] static std::shared_ptr<const RunConfigurationControl>
    run_control(const MeshSnapshot<dim>& snapshot) noexcept
    {
        return snapshot.run_control_;
    }
};

/**
 * \brief Allocate a final immutable snapshot behind the fatal cleanup boundary.
 * \tparam dim triangulation dimension.
 * \param run_control retained run lifetime and fatal policy.
 * \param storage communicator-stable triangulation storage.
 * \param provenance allocated run and mesh identity.
 * \param allocator immutable snapshot allocation operation.
 * \return allocated immutable snapshot.
 *
 * A rank-local allocation failure abandons the distributed communicator
 * before invoking the fatal handler. Serial storage has no communicator
 * control and follows the same boundary without cleanup work.
 */
template<int dim>
[[nodiscard]] std::expected<std::shared_ptr<const MeshSnapshot<dim>>, MeshSnapshotError>
allocate_mesh_snapshot_with_allocator(std::shared_ptr<const RunConfigurationControl> run_control,
                                      MeshSnapshotStorage<dim> storage, const MeshSnapshotProvenance provenance,
                                      const MeshSnapshotAllocator<dim> allocator)
{
    const auto fatal_control = run_control;
    const auto retained_mesh_communicator = storage.communicator_control;
    try {
        return allocator(std::move(run_control), std::move(storage.communicator_control),
                         std::move(storage.triangulation), provenance, storage.communicator);
    }
    catch (const std::bad_alloc&) {
        abandon_mesh_communicator(retained_mesh_communicator);
        abort_mesh_snapshot_allocation(fatal_control);
    }
}

/**
 * \brief Implement mesh construction with a private allocation seam.
 * \tparam dim triangulation dimension.
 * \param run run defining the collective construction.
 * \param triangulation transferred triangulation, consumed on every outcome.
 * \param allocator immutable snapshot allocation operation.
 * \return immutable snapshot or deterministic logical error.
 */
template<int dim>
[[nodiscard]] std::expected<std::shared_ptr<const MeshSnapshot<dim>>, MeshSnapshotError>
make_mesh_snapshot_with_allocator(const RunConfiguration& run,
                                  std::unique_ptr<dealii::Triangulation<dim>> triangulation,
                                  const MeshSnapshotAllocator<dim> allocator)
{
    auto run_control = retain_mesh_snapshot_run_context(run, triangulation != nullptr);
    if (!run_control) {
        return std::unexpected(std::move(run_control).error());
    }

    const MPI_Comm mesh_communicator = triangulation->get_mpi_communicator();
    const auto storage_classification = classify_mesh_storage(*triangulation);
    auto preparation = prepare_mesh_snapshot(*run_control, mesh_communicator, storage_classification);
    if (!preparation) {
        return std::unexpected(std::move(preparation).error());
    }

    auto storage = stabilize_mesh_storage(preparation->run_control, std::move(triangulation), mesh_communicator);
    return allocate_mesh_snapshot_with_allocator(std::move(preparation->run_control), std::move(storage),
                                                 preparation->provenance, allocator);
}

} // namespace detail

/**
 * \brief Collectively transfer a triangulation into an immutable run-owned snapshot.
 *
 * Every run rank must call in the same collective order. All non-null
 * triangulations must have been constructed on the same MPI communicator
 * context and must have no external mutators or subscribers. Rift can validate
 * each context against the run but MPI cannot portably prove that ranks did not
 * alternate distinct congruent contexts.
 *
 * The input pointer is consumed on success and failure. Serial triangulations
 * use `MPI_COMM_SELF`. Nonempty
 * `dealii::parallel::distributed::Triangulation` objects are reconstructed on
 * an owned duplicate of their communicator; empty distributed meshes are
 * rejected because deal.II 9.8 cannot copy them. Other parallel
 * triangulation kinds are rejected rather than silently changing their type
 * or partitioning semantics. Only `MPI_IDENT` and `MPI_CONGRUENT`
 * relationships with the run are accepted.
 *
 * \tparam dim triangulation dimension.
 * \param run retained run defining collective validation and identity allocation.
 * \param triangulation exclusively owned completed mesh.
 * \return immutable shared snapshot or communicator-consistent logical error.
 */
template<int dim>
[[nodiscard]] std::expected<std::shared_ptr<const MeshSnapshot<dim>>, MeshSnapshotError>
make_mesh_snapshot(const RunConfiguration& run, std::unique_ptr<dealii::Triangulation<dim>> triangulation)
{
    return detail::make_mesh_snapshot_with_allocator(run, std::move(triangulation),
                                                     detail::MeshSnapshotFactory<dim>::create);
}

} // namespace rift
