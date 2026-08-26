/**
 * \file
 * \brief Collective validation and identity allocation for immutable meshes.
 */

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <mpi.h>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include "mesh_snapshot_internal.hpp"
#include "run_configuration_internal.hpp"

#include <mutex>
#include <new>
#include <numeric>
#include <rift/mesh_snapshot.hpp>
#include <rift/run_configuration.hpp>
#include <utility>
#include <vector>

namespace rift::detail {

namespace {

/**
 * \brief Invoke the retained fatal handler and make non-return explicit.
 * \param control run control retaining communicator and fatal policy.
 * \param status failed MPI status or `MPI_ERR_NO_MEM`.
 */
[[noreturn]] void invoke_mesh_fatal(const std::shared_ptr<const RunConfigurationControl>& control, const int status)
{
    control->abort_handler()(control->communicator(), status);
    std::unreachable();
}

/**
 * \brief Reduce one classification to the deterministic run-wide minimum.
 * \param local local classification representation.
 * \param agreed destination run-wide minimum.
 * \param communicator retained run communicator.
 * \return MPI status.
 */
int mpi_collective_integer_minimum(const int local, int& agreed, const MPI_Comm communicator)
{
    return MPI_Allreduce(&local, &agreed, 1, MPI_INT, MPI_MIN, communicator);
}

/**
 * \brief Broadcast one mesh sequence from retained-run rank zero.
 * \param sequence sequence value or exhaustion sentinel.
 * \param communicator retained run communicator.
 * \return MPI status.
 */
int mpi_broadcast_mesh_sequence(std::uint32_t& sequence, const MPI_Comm communicator)
{
    return MPI_Bcast(&sequence, 1, MPI_UINT32_T, 0, communicator);
}

/**
 * \brief Share the process-global mesh sequence state for this world rank.
 * \return allocator whose values are never reset during the MPI execution.
 */
MeshSequenceAllocator& process_mesh_sequence_allocator()
{
    static MeshSequenceAllocator allocator;
    return allocator;
}

/**
 * \brief Create the public deterministic error for one agreed invalid classification.
 * \param classification run-wide invalid classification; `valid` is excluded by the caller.
 * \return corresponding structured error.
 */
MeshSnapshotError classification_error(const MeshCommunicatorClassification classification)
{
    constexpr std::array codes{
        MeshSnapshotErrorCode::intercommunicator_not_supported,
        MeshSnapshotErrorCode::communicator_not_world_derived,
        MeshSnapshotErrorCode::communicator_reordered,
        MeshSnapshotErrorCode::communicator_mismatch,
    };
    constexpr std::array messages{
        "mesh snapshots require an MPI intracommunicator",
        "one or more mesh communicator members do not belong to MPI_COMM_WORLD",
        "mesh communicator rank order differs from the run communicator",
        "mesh and run communicators contain different process groups",
    };
    const auto index = static_cast<std::size_t>(classification);
    return {.code = codes.at(index), .message = messages.at(index)};
}

/** \brief Convert one agreed storage classification to its public diagnostic. */
MeshSnapshotError storage_classification_error(const MeshStorageClassification classification)
{
    if (classification == MeshStorageClassification::unsupported) {
        return {.code = MeshSnapshotErrorCode::unsupported_triangulation_kind,
                .message = "only serial and parallel::distributed triangulations are supported"};
    }
    return {.code = MeshSnapshotErrorCode::empty_distributed_triangulation,
            .message = "an empty parallel::distributed triangulation cannot be retained"};
}

} // namespace

std::expected<std::uint32_t, MeshSnapshotError> MeshSequenceAllocator::reserve()
{
    const std::scoped_lock lock(mutex_);
    constexpr auto exhausted = static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
    if (next_sequence_ >= exhausted) {
        return std::unexpected(MeshSnapshotError{.code = MeshSnapshotErrorCode::id_space_exhausted,
                                                 .message = "mesh snapshot identity sequence space is exhausted"});
    }
    return static_cast<std::uint32_t>(next_sequence_++);
}

MeshSnapshotMpiOperations mesh_snapshot_mpi_operations() noexcept
{
    return {.initialized = MPI_Initialized,
            .finalized = MPI_Finalized,
            .collective_minimum = mpi_collective_integer_minimum,
            .test_intercommunicator = MPI_Comm_test_inter,
            .communicator_size = MPI_Comm_size,
            .allocate_rank_buffer = allocate_rank_buffer,
            .communicator_group = MPI_Comm_group,
            .translate_ranks = MPI_Group_translate_ranks,
            .free_group = MPI_Group_free,
            .compare = MPI_Comm_compare,
            .communicator_rank = MPI_Comm_rank,
            .broadcast_sequence = mpi_broadcast_mesh_sequence};
}

MeshCommunicatorControl::~MeshCommunicatorControl()
{
    if (communicator_ != MPI_COMM_NULL) {
        release_mesh_communicator_with_operations(communicator_, cleanup_operations_);
    }
}

void release_mesh_communicator_with_operations(MPI_Comm& communicator, const MpiCleanupOperations operations)
{
    release_communicator(communicator, operations);
}

RetainedMeshCommunicator own_mesh_communicator(const std::shared_ptr<const RunConfigurationControl>& run_control,
                                               const MPI_Comm source)
{
    return own_mesh_communicator_with_operations(run_control, source, MPI_Comm_dup, allocate_mesh_communicator_control);
}

std::shared_ptr<const MeshCommunicatorControl> allocate_mesh_communicator_control(const MPI_Comm communicator,
                                                                                  const MpiAbort abort)
{
    return std::make_shared<MeshCommunicatorControl>(communicator,
                                                     MpiCleanupOperations{.initialized = MPI_Initialized,
                                                                          .finalized = MPI_Finalized,
                                                                          .free_communicator = MPI_Comm_free,
                                                                          .abort = abort});
}

void abandon_mesh_communicator(const std::shared_ptr<const MeshCommunicatorControl>& control) noexcept
{
    if (control != nullptr) {
        control->abandon();
    }
}

RetainedMeshCommunicator
own_mesh_communicator_with_operations(const std::shared_ptr<const RunConfigurationControl>& run_control,
                                      const MPI_Comm source, const MpiCommDuplicate duplicate_operation,
                                      const MeshCommunicatorControlAllocator allocate_control)
{
    MPI_Comm duplicate = MPI_COMM_NULL;
    const int status = duplicate_operation(source, &duplicate);
    if (status != MPI_SUCCESS) {
        invoke_mesh_fatal(run_control, status);
    }
    try {
        auto control = allocate_control(duplicate, run_control->abort_handler());
        return {.control = std::move(control), .communicator = duplicate};
    }
    catch (const std::bad_alloc&) {
        invoke_mesh_fatal(run_control, MPI_ERR_NO_MEM);
    }
}

std::expected<std::shared_ptr<const RunConfigurationControl>, MeshSnapshotError>
retain_mesh_snapshot_run_context_with_operations(const RunConfiguration& run, const bool mesh_present,
                                                 const MeshSnapshotMpiOperations& operations)
{
    auto control = RunConfigurationAccess::control(run);
    int initialized = 0;
    int status = operations.initialized(&initialized);
    if (status != MPI_SUCCESS) {
        invoke_mesh_fatal(control, status);
    }
    if (initialized == 0) {
        return std::unexpected(MeshSnapshotError{.code = MeshSnapshotErrorCode::mpi_not_initialized,
                                                 .message = "MPI must be initialized before creating a mesh snapshot"});
    }

    int finalized = 0;
    status = operations.finalized(&finalized);
    if (status != MPI_SUCCESS) {
        invoke_mesh_fatal(control, status);
    }
    if (finalized != 0) {
        return std::unexpected(MeshSnapshotError{.code = MeshSnapshotErrorCode::mpi_finalized,
                                                 .message = "MPI is finalized; a mesh snapshot cannot be created"});
    }

    const int local_presence = mesh_present ? 1 : 0;
    int every_rank_present = 0;
    status = operations.collective_minimum(local_presence, every_rank_present, control->communicator());
    if (status != MPI_SUCCESS) {
        invoke_mesh_fatal(control, status);
    }
    if (every_rank_present == 0) {
        return std::unexpected(MeshSnapshotError{.code = MeshSnapshotErrorCode::null_triangulation,
                                                 .message = "every run rank must supply a triangulation"});
    }
    return control;
}

MeshCommunicatorClassification
classify_mesh_communicator(const std::shared_ptr<const RunConfigurationControl>& run_control,
                           const MPI_Comm mesh_communicator, const MeshSnapshotMpiOperations& operations)
{
    if (mesh_communicator == MPI_COMM_NULL) {
        return MeshCommunicatorClassification::mismatch;
    }

    int is_intercommunicator = 0;
    int status = operations.test_intercommunicator(mesh_communicator, &is_intercommunicator);
    if (status != MPI_SUCCESS) {
        invoke_mesh_fatal(run_control, status);
    }
    if (is_intercommunicator != 0) {
        return MeshCommunicatorClassification::intercommunicator;
    }

    int communicator_size = 0;
    status = operations.communicator_size(mesh_communicator, &communicator_size);
    if (status != MPI_SUCCESS) {
        invoke_mesh_fatal(run_control, status);
    }

    std::vector<int> communicator_ranks;
    std::vector<int> world_ranks;
    try {
        communicator_ranks = operations.allocate_rank_buffer(communicator_size);
        world_ranks = operations.allocate_rank_buffer(communicator_size);
    }
    catch (const std::bad_alloc&) {
        invoke_mesh_fatal(run_control, MPI_ERR_NO_MEM);
    }
    std::ranges::iota(communicator_ranks, 0);

    MPI_Group communicator_group = MPI_GROUP_NULL;
    status = operations.communicator_group(mesh_communicator, &communicator_group);
    if (status != MPI_SUCCESS) {
        invoke_mesh_fatal(run_control, status);
    }
    MPI_Group world_group = MPI_GROUP_NULL;
    status = operations.communicator_group(MPI_COMM_WORLD, &world_group);
    if (status != MPI_SUCCESS) {
        invoke_mesh_fatal(run_control, status);
    }
    status = operations.translate_ranks(communicator_group, communicator_size, communicator_ranks.data(), world_group,
                                        world_ranks.data());
    if (status != MPI_SUCCESS) {
        invoke_mesh_fatal(run_control, status);
    }
    status = operations.free_group(&communicator_group);
    if (status != MPI_SUCCESS) {
        invoke_mesh_fatal(run_control, status);
    }
    status = operations.free_group(&world_group);
    if (status != MPI_SUCCESS) {
        invoke_mesh_fatal(run_control, status);
    }

    if (std::ranges::any_of(world_ranks, [](const int rank) { return rank == MPI_UNDEFINED; })) {
        return MeshCommunicatorClassification::not_world_derived;
    }

    int relationship = MPI_UNEQUAL;
    status = operations.compare(mesh_communicator, run_control->communicator(), &relationship);
    if (status != MPI_SUCCESS) {
        invoke_mesh_fatal(run_control, status);
    }
    if (relationship == MPI_IDENT || relationship == MPI_CONGRUENT) {
        return MeshCommunicatorClassification::valid;
    }
    if (relationship == MPI_SIMILAR) {
        return MeshCommunicatorClassification::reordered;
    }
    return MeshCommunicatorClassification::mismatch;
}

std::expected<MeshSnapshotId, MeshSnapshotError>
reserve_mesh_snapshot_id(const std::shared_ptr<const RunConfigurationControl>& run_control,
                         const MeshSnapshotMpiOperations& operations, MeshSequenceAllocator& allocator)
{
    int rank = 0;
    int status = operations.communicator_rank(run_control->communicator(), &rank);
    if (status != MPI_SUCCESS) {
        invoke_mesh_fatal(run_control, status);
    }

    constexpr auto exhausted = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t sequence = exhausted;
    if (rank == 0) {
        const auto reserved = allocator.reserve();
        if (reserved) {
            sequence = *reserved;
        }
    }
    status = operations.broadcast_sequence(sequence, run_control->communicator());
    if (status != MPI_SUCCESS) {
        invoke_mesh_fatal(run_control, status);
    }
    if (sequence == exhausted) {
        return std::unexpected(MeshSnapshotError{.code = MeshSnapshotErrorCode::id_space_exhausted,
                                                 .message = "mesh snapshot identity sequence space is exhausted"});
    }

    const auto origin = static_cast<std::uint32_t>(run_control->id().value() >> 32U);
    const auto encoded = (static_cast<std::uint64_t>(origin) << 32U) | sequence;
    return MeshSnapshotId::from_index(encoded);
}

std::expected<MeshSnapshotPreparation, MeshSnapshotError>
prepare_mesh_snapshot_with_operations(std::shared_ptr<const RunConfigurationControl> run_control,
                                      const MPI_Comm mesh_communicator,
                                      const MeshStorageClassification storage_classification,
                                      const MeshSnapshotMpiOperations& operations, MeshSequenceAllocator& allocator)
{
    try {
        const auto local_classification = classify_mesh_communicator(run_control, mesh_communicator, operations);
        int agreed_classification = static_cast<int>(MeshCommunicatorClassification::valid);
        const int status = operations.collective_minimum(static_cast<int>(local_classification), agreed_classification,
                                                         run_control->communicator());
        if (status != MPI_SUCCESS) {
            invoke_mesh_fatal(run_control, status);
        }
        const auto agreed = static_cast<MeshCommunicatorClassification>(agreed_classification);
        if (agreed != MeshCommunicatorClassification::valid) {
            return std::unexpected(classification_error(agreed));
        }

        int agreed_storage = static_cast<int>(MeshStorageClassification::valid);
        const int storage_status = operations.collective_minimum(static_cast<int>(storage_classification),
                                                                 agreed_storage, run_control->communicator());
        if (storage_status != MPI_SUCCESS) {
            invoke_mesh_fatal(run_control, storage_status);
        }
        const auto agreed_storage_classification = static_cast<MeshStorageClassification>(agreed_storage);
        if (agreed_storage_classification != MeshStorageClassification::valid) {
            return std::unexpected(storage_classification_error(agreed_storage_classification));
        }

        auto id = reserve_mesh_snapshot_id(run_control, operations, allocator);
        if (!id) {
            return std::unexpected(std::move(id).error());
        }
        const auto run_id = run_control->id();
        return MeshSnapshotPreparation{.run_control = std::move(run_control),
                                       .communicator = mesh_communicator,
                                       .provenance = {.run = run_id, .mesh = *id}};
    }
    catch (const std::bad_alloc&) {
        invoke_mesh_fatal(run_control, MPI_ERR_NO_MEM);
    }
}

std::expected<std::shared_ptr<const RunConfigurationControl>, MeshSnapshotError>
retain_mesh_snapshot_run_context(const RunConfiguration& run, const bool mesh_present)
{
    return retain_mesh_snapshot_run_context_with_operations(run, mesh_present, mesh_snapshot_mpi_operations());
}

std::expected<MeshSnapshotPreparation, MeshSnapshotError>
prepare_mesh_snapshot(std::shared_ptr<const RunConfigurationControl> run_control, const MPI_Comm mesh_communicator,
                      const MeshStorageClassification storage_classification)
{
    return prepare_mesh_snapshot_with_operations(std::move(run_control), mesh_communicator, storage_classification,
                                                 mesh_snapshot_mpi_operations(), process_mesh_sequence_allocator());
}

[[noreturn]] void abort_mesh_snapshot_allocation(const std::shared_ptr<const RunConfigurationControl>& run_control)
{
    invoke_mesh_fatal(run_control, MPI_ERR_NO_MEM);
}

[[noreturn]] void abort_mesh_snapshot_reconstruction(const std::shared_ptr<const RunConfigurationControl>& run_control,
                                                     const int status)
{
    invoke_mesh_fatal(run_control, status);
}

} // namespace rift::detail
