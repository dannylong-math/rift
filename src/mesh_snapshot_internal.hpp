#pragma once

/**
 * \file
 * \brief Private MPI validation and identity allocation for immutable meshes.
 */

#include "run_configuration_internal.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <mpi.h>
#include <mutex>
#include <rift/mesh_snapshot.hpp>
#include <rift/run_configuration.hpp>

namespace rift::detail {

/** \brief MPI signature used to compare communicator groups and rank order. */
using MpiCommCompare = int (*)(MPI_Comm, MPI_Comm, int*);

/** \brief MPI signature used to duplicate a distributed-mesh communicator. */
using MpiCommDuplicate = int (*)(MPI_Comm, MPI_Comm*);

/** \brief Narrow collective-minimum operation over the retained run. */
using CollectiveIntegerMinimum = int (*)(int, int&, MPI_Comm);

/** \brief Narrow broadcast operation for a finite mesh sequence. */
using MeshSequenceBroadcast = int (*)(std::uint32_t&, MPI_Comm);

/**
 * \brief Collect MPI dependencies used by mesh validation and ID allocation.
 *
 * Private tests replace individual operations with deterministic seams;
 * production constructs the complete set with MPI functions and narrow
 * wrappers.
 *
 * \code{.cpp}
 * const auto operations = rift::detail::mesh_snapshot_mpi_operations();
 * int agreed = 0;
 * operations.collective_minimum(1, agreed, MPI_COMM_SELF);
 * \endcode
 */
struct MeshSnapshotMpiOperations {
    /** \brief MPI initialization query. */
    MpiLifecycleQuery initialized;
    /** \brief MPI finalization query. */
    MpiLifecycleQuery finalized;
    /** \brief Run-communicator exact integer agreement. */
    CollectiveIntegerMinimum collective_minimum;
    /** \brief Mesh communicator-kind query. */
    MpiCommTestInter test_intercommunicator;
    /** \brief Mesh communicator-size query. */
    MpiCommSize communicator_size;
    /** \brief Rank-buffer allocation operation. */
    RankBufferAllocator allocate_rank_buffer;
    /** \brief Communicator-group query. */
    MpiCommGroup communicator_group;
    /** \brief Complete group translation into the current world. */
    MpiGroupTranslateRanks translate_ranks;
    /** \brief MPI group cleanup. */
    MpiGroupFree free_group;
    /** \brief Mesh/run communicator comparison. */
    MpiCommCompare compare;
    /** \brief Retained run rank query. */
    MpiCommRank communicator_rank;
    /** \brief Mesh-ID sequence broadcast on the run. */
    MeshSequenceBroadcast broadcast_sequence;
};

/**
 * \brief Own one duplicated communicator used by a distributed triangulation.
 *
 * The triangulation is destroyed before the last snapshot reference releases
 * this control. The control then frees the communicator while MPI remains
 * active and uses the retained fatal policy for status failures.
 *
 * \code{.cpp}
 * const auto retained = rift::detail::own_mesh_communicator(control, source);
 * const MPI_Comm communicator = retained.communicator;
 * \endcode
 */
class MeshCommunicatorControl {
public:
    /**
     * \brief Adopt an already duplicated communicator.
     * \param communicator owned communicator.
     * \param cleanup_operations lifecycle, release, and fatal operations.
     */
    MeshCommunicatorControl(MPI_Comm communicator, MpiCleanupOperations cleanup_operations) noexcept :
        communicator_(communicator), cleanup_operations_(cleanup_operations)
    {
    }

    /** \brief Free the owned communicator before MPI finalization. */
    ~MeshCommunicatorControl();

    MeshCommunicatorControl(const MeshCommunicatorControl&) = delete;
    MeshCommunicatorControl(MeshCommunicatorControl&&) = delete;
    MeshCommunicatorControl& operator=(const MeshCommunicatorControl&) = delete;
    MeshCommunicatorControl& operator=(MeshCommunicatorControl&&) = delete;

    /** \brief Borrow the owned communicator. */
    [[nodiscard]] MPI_Comm communicator() const noexcept { return communicator_; }

    /**
     * \brief Relinquish cleanup when a rank-local fatal failure prevents collective release.
     *
     * MPI teardown reclaims the communicator after the fatal handler terminates
     * the job. This operation is used immediately before invoking that handler.
     */
    void abandon() const noexcept { communicator_ = MPI_COMM_NULL; }

private:
    /** \brief Owned duplicate. */
    mutable MPI_Comm communicator_;
    /** \brief Lifecycle, release, and fatal operations used by destruction. */
    MpiCleanupOperations cleanup_operations_;
};

/** \brief Allocate retained control for an already duplicated mesh communicator. */
using MeshCommunicatorControlAllocator = std::shared_ptr<const MeshCommunicatorControl> (*)(MPI_Comm, MpiAbort);

/**
 * \brief Allocate production mesh-communicator control.
 * \param communicator already duplicated communicator to adopt.
 * \param abort retained fatal handler.
 * \return shared RAII control.
 */
[[nodiscard]] std::shared_ptr<const MeshCommunicatorControl> allocate_mesh_communicator_control(MPI_Comm communicator,
                                                                                                MpiAbort abort);

/**
 * \brief Duplicate and retain a mesh communicator behind private failure seams.
 * \param run_control retained fatal policy.
 * \param source validated source communicator.
 * \param duplicate injected duplication operation.
 * \param allocate_control injected shared-control allocation.
 * \return owned duplicate and its shared control.
 */
[[nodiscard]] RetainedMeshCommunicator
own_mesh_communicator_with_operations(const std::shared_ptr<const RunConfigurationControl>& run_control,
                                      MPI_Comm source, MpiCommDuplicate duplicate,
                                      MeshCommunicatorControlAllocator allocate_control);

/**
 * \brief Release an owned mesh communicator behind lifecycle and free-operation seams.
 * \param communicator owned communicator, set null after successful release.
 * \param operations injected cleanup lifecycle, free, and fatal operations.
 */
void release_mesh_communicator_with_operations(MPI_Comm& communicator, MpiCleanupOperations operations);

/**
 * \brief Reserve process-global mesh sequences for one world-rank origin.
 *
 * The allocator is used only when the local process is communicator rank zero.
 * Its maximum `uint32_t` representation is an exhaustion sentinel.
 *
 * \code{.cpp}
 * rift::detail::MeshSequenceAllocator allocator;
 * const auto first = allocator.reserve().value();
 * \endcode
 */
class MeshSequenceAllocator {
public:
    /** \brief Select the first unreserved sequence, normally zero. */
    explicit MeshSequenceAllocator(std::uint64_t next_sequence = 0) noexcept : next_sequence_(next_sequence) {}

    /** \brief Reserve a finite sequence or report exhaustion without wraparound. */
    [[nodiscard]] std::expected<std::uint32_t, MeshSnapshotError> reserve();

private:
    /** \brief Serialize process-global sequence reservation. */
    std::mutex mutex_;
    /** \brief First sequence representation not yet consumed. */
    std::uint64_t next_sequence_;
};

/**
 * \brief Describe the local communicator classification before run agreement.
 *
 * Lower numeric values have earlier deterministic error precedence. `valid`
 * sorts last so any rank-local error wins the run collective minimum.
 *
 * \code{.cpp}
 * const auto classification =
 *     rift::detail::MeshCommunicatorClassification::valid;
 * \endcode
 */
enum class MeshCommunicatorClassification : std::uint8_t {
    /** \brief Mesh uses an intercommunicator. */
    intercommunicator = 0,
    /** \brief A mesh member is outside the current MPI world. */
    not_world_derived = 1,
    /** \brief Mesh group matches but rank order differs. */
    reordered = 2,
    /** \brief Mesh and run groups differ or mesh communicator is null. */
    mismatch = 3,
    /** \brief Mesh and run communicators are identical or congruent. */
    valid = 4,
};

/** \brief Return production MPI operations for mesh construction. */
[[nodiscard]] MeshSnapshotMpiOperations mesh_snapshot_mpi_operations() noexcept;

/**
 * \brief Collectively agree that every run rank supplied a triangulation.
 * \param run source run handle.
 * \param mesh_present whether the local transferred pointer is non-null.
 * \param operations injected MPI queries, agreement, and fatal handler.
 * \return retained run control or deterministic lifecycle/null error.
 */
[[nodiscard]] std::expected<std::shared_ptr<const RunConfigurationControl>, MeshSnapshotError>
retain_mesh_snapshot_run_context_with_operations(const RunConfiguration& run, bool mesh_present,
                                                 const MeshSnapshotMpiOperations& operations);

/**
 * \brief Classify one mesh communicator using only local MPI group operations.
 * \param run_control retained run communicator and fatal handler.
 * \param mesh_communicator communicator derived from the local triangulation.
 * \param operations injected MPI group and comparison operations.
 * \return local classification for subsequent run agreement.
 */
[[nodiscard]] MeshCommunicatorClassification
classify_mesh_communicator(const std::shared_ptr<const RunConfigurationControl>& run_control,
                           MPI_Comm mesh_communicator, const MeshSnapshotMpiOperations& operations);

/**
 * \brief Reserve a globally unique origin-plus-sequence mesh identity.
 * \param run_control retained run communicator and encoded world origin.
 * \param operations rank, broadcast, and fatal operations.
 * \param allocator process-global sequence allocator used by run rank zero.
 * \return communicator-consistent ID or finite exhaustion.
 */
[[nodiscard]] std::expected<MeshSnapshotId, MeshSnapshotError>
reserve_mesh_snapshot_id(const std::shared_ptr<const RunConfigurationControl>& run_control,
                         const MeshSnapshotMpiOperations& operations, MeshSequenceAllocator& allocator);

/**
 * \brief Validate communicator classification collectively and allocate provenance.
 * \param run_control retained run context after collective null agreement.
 * \param mesh_communicator authoritative triangulation communicator.
 * \param storage_classification local triangulation kind and empty-state classification.
 * \param operations injected MPI operations.
 * \param allocator process-global mesh sequence allocator.
 * \return allocation preparation or agreed logical error.
 */
[[nodiscard]] std::expected<MeshSnapshotPreparation, MeshSnapshotError>
prepare_mesh_snapshot_with_operations(std::shared_ptr<const RunConfigurationControl> run_control,
                                      MPI_Comm mesh_communicator, MeshStorageClassification storage_classification,
                                      const MeshSnapshotMpiOperations& operations, MeshSequenceAllocator& allocator);

} // namespace rift::detail
