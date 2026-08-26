/**
 * \file
 * \brief Collective run creation and owned communicator implementation.
 */

#include "run_configuration_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <mpi.h>
#include <mutex>
#include <new>
#include <numeric>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <rift/run_configuration.hpp>
#include <string>
#include <utility>
#include <vector>

namespace rift {
namespace {

/**
 * \brief Access the process-local sequence allocator for runs originated here.
 * \return finite allocator with static process lifetime.
 */
detail::RunSequenceAllocator& run_sequence_allocator() noexcept
{
    static detail::RunSequenceAllocator allocator;
    return allocator;
}

/**
 * \brief Adapt MPI allreduce to the graph allocator's narrow maximum operation.
 * \param local local candidate.
 * \param chosen communicator-wide maximum destination.
 * \param communicator collective communicator.
 * \return MPI status code.
 */
int mpi_collective_maximum(const std::uint64_t local, std::uint64_t& chosen, const MPI_Comm communicator)
{
    return MPI_Allreduce(&local, &chosen, 1, MPI_UINT64_T, MPI_MAX, communicator);
}

} // namespace

namespace detail {
namespace {

/**
 * \brief Invoke the configured fatal MPI operation and make non-return explicit.
 * \param abort MPI-compatible fatal handler.
 * \param communicator communicator associated with the failed operation.
 * \param status MPI status returned by the failed operation.
 */
[[noreturn]] void invoke_mpi_abort(const MpiAbort abort, const MPI_Comm communicator, const int status)
{
    abort(communicator, status);
    std::unreachable();
}

} // namespace

std::expected<std::uint32_t, RunConfigurationError> RunSequenceAllocator::reserve() noexcept
{
    const std::scoped_lock lock(mutex_);
    constexpr auto exhausted = std::numeric_limits<std::uint32_t>::max();
    if (next_sequence_ >= exhausted) {
        return std::unexpected(
            RunConfigurationError{.code = RunConfigurationErrorCode::id_space_exhausted,
                                  .message = "run identity sequence space is exhausted for this MPI world rank"});
    }
    const auto sequence = static_cast<std::uint32_t>(next_sequence_);
    ++next_sequence_;
    return sequence;
}

std::uint64_t MonotonicIdAllocator::reserve_local_candidate() noexcept
{
    const std::scoped_lock lock(mutex_);
    return reserve_local_candidate_unlocked();
}

std::uint64_t MonotonicIdAllocator::reserve_local_candidate_unlocked() noexcept
{
    constexpr auto exhausted = std::numeric_limits<std::uint64_t>::max();
    if (next_candidate_ == exhausted) {
        return exhausted;
    }
    const auto candidate = next_candidate_;
    ++next_candidate_;
    return candidate;
}

void MonotonicIdAllocator::advance_beyond(const std::uint64_t chosen) noexcept
{
    const std::scoped_lock lock(mutex_);
    advance_beyond_unlocked(chosen);
}

void MonotonicIdAllocator::advance_beyond_unlocked(const std::uint64_t chosen) noexcept
{
    if (next_candidate_ <= chosen) {
        next_candidate_ = chosen + 1;
    }
}

std::expected<void, RunConfigurationError> validate_mpi_lifecycle(const MpiLifecycleState state, const MpiAbort abort)
{
    if (state.initialized_status != MPI_SUCCESS) {
        invoke_mpi_abort(abort, state.communicator, state.initialized_status);
    }
    if (state.initialized == 0) {
        return std::unexpected(
            RunConfigurationError{.code = RunConfigurationErrorCode::mpi_not_initialized,
                                  .message = "MPI must be initialized before creating a run configuration"});
    }
    if (state.finalized_status != MPI_SUCCESS) {
        invoke_mpi_abort(abort, state.communicator, state.finalized_status);
    }
    if (state.finalized != 0) {
        return std::unexpected(
            RunConfigurationError{.code = RunConfigurationErrorCode::mpi_finalized,
                                  .message = "MPI is finalized; a run configuration cannot be created"});
    }
    if (state.communicator == MPI_COMM_NULL) {
        return std::unexpected(
            RunConfigurationError{.code = RunConfigurationErrorCode::null_communicator,
                                  .message = "a run configuration requires a non-null MPI communicator"});
    }
    return {};
}

std::expected<void, RunConfigurationError> validate_intracommunicator(const MPI_Comm communicator,
                                                                      const MpiCommTestInter test_intercommunicator,
                                                                      const MpiAbort abort)
{
    int is_intercommunicator = 0;
    const int status = test_intercommunicator(communicator, &is_intercommunicator);
    if (status != MPI_SUCCESS) {
        invoke_mpi_abort(abort, communicator, status);
    }
    if (is_intercommunicator != 0) {
        return std::unexpected(
            RunConfigurationError{.code = RunConfigurationErrorCode::intercommunicator_not_supported,
                                  .message = "Rift run configurations require an MPI intracommunicator"});
    }
    return {};
}

std::expected<std::pair<int, int>, RunConfigurationError>
communicator_rank_and_origin(const MPI_Comm communicator, const MpiRunOperations& operations)
{
    int communicator_rank = 0;
    int status = operations.communicator_rank(communicator, &communicator_rank);
    if (status != MPI_SUCCESS) {
        invoke_mpi_abort(operations.abort, communicator, status);
    }

    int communicator_size = 0;
    status = operations.communicator_size(communicator, &communicator_size);
    if (status != MPI_SUCCESS) {
        invoke_mpi_abort(operations.abort, communicator, status);
    }

    std::vector<int> communicator_ranks;
    std::vector<int> world_ranks;
    try {
        communicator_ranks = operations.allocate_rank_buffer(communicator_size);
        world_ranks = operations.allocate_rank_buffer(communicator_size);
    }
    catch (const std::bad_alloc&) {
        invoke_mpi_abort(operations.abort, communicator, MPI_ERR_NO_MEM);
    }
    std::ranges::iota(communicator_ranks, 0);

    MPI_Group communicator_group = MPI_GROUP_NULL;
    status = operations.communicator_group(communicator, &communicator_group);
    if (status != MPI_SUCCESS) {
        invoke_mpi_abort(operations.abort, communicator, status);
    }
    MPI_Group world_group = MPI_GROUP_NULL;
    status = operations.communicator_group(MPI_COMM_WORLD, &world_group);
    if (status != MPI_SUCCESS) {
        invoke_mpi_abort(operations.abort, communicator, status);
    }

    status = operations.translate_ranks(communicator_group, communicator_size, communicator_ranks.data(), world_group,
                                        world_ranks.data());
    if (status != MPI_SUCCESS) {
        invoke_mpi_abort(operations.abort, communicator, status);
    }
    status = operations.free_group(&communicator_group);
    if (status != MPI_SUCCESS) {
        invoke_mpi_abort(operations.abort, communicator, status);
    }
    status = operations.free_group(&world_group);
    if (status != MPI_SUCCESS) {
        invoke_mpi_abort(operations.abort, communicator, status);
    }

    const bool all_members_supported =
        std::ranges::none_of(world_ranks, [](const int rank) { return rank == MPI_UNDEFINED; });
    const std::uint64_t unsupported_origin = all_members_supported ? 0U : 1U;
    std::uint64_t any_unsupported_origin = 0;
    status = operations.collective_maximum(unsupported_origin, any_unsupported_origin, communicator);
    if (status != MPI_SUCCESS) {
        invoke_mpi_abort(operations.abort, communicator, status);
    }
    if (any_unsupported_origin != 0) {
        return std::unexpected(
            RunConfigurationError{.code = RunConfigurationErrorCode::communicator_not_world_derived,
                                  .message = "one or more communicator members do not belong to MPI_COMM_WORLD"});
    }
    return std::pair{communicator_rank, world_ranks.front()};
}

std::vector<int> allocate_rank_buffer(const int size) { return std::vector<int>(static_cast<std::size_t>(size)); }

void release_communicator(MPI_Comm& communicator, const MpiCleanupOperations operations)
{
    int initialized = 0;
    int status = operations.initialized(&initialized);
    if (status != MPI_SUCCESS) {
        invoke_mpi_abort(operations.abort, communicator, status);
    }
    if (initialized == 0) {
        return;
    }

    int finalized = 0;
    status = operations.finalized(&finalized);
    if (status != MPI_SUCCESS) {
        invoke_mpi_abort(operations.abort, communicator, status);
    }
    if (finalized != 0) {
        return;
    }

    const MPI_Comm fatal_communicator = communicator;
    status = operations.free_communicator(&communicator);
    if (status != MPI_SUCCESS) {
        invoke_mpi_abort(operations.abort, fatal_communicator, status);
    }
}

std::shared_ptr<const RunConfigurationControl> allocate_run_control(const MPI_Comm communicator,
                                                                    const RunConfigurationId id, const MpiAbort abort)
{
    return std::make_shared<RunConfigurationControl>(communicator, id, 0, abort);
}

MPI_Comm duplicate_communicator(const MPI_Comm communicator, const MpiCommDup duplicate, const MpiAbort abort)
{
    MPI_Comm result = MPI_COMM_NULL;
    const int status = duplicate(communicator, &result);
    if (status != MPI_SUCCESS) {
        invoke_mpi_abort(abort, communicator, status);
    }
    return result;
}

std::expected<std::uint64_t, RunConfigurationError> reserve_run_id(RunSequenceAllocator& allocator,
                                                                   const RunIdentityContext context,
                                                                   const MpiBroadcast broadcast, const MpiAbort abort)
{
    constexpr auto exhausted = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t sequence = exhausted;
    if (context.communicator_rank == 0) {
        const auto reserved = allocator.reserve();
        if (reserved) {
            sequence = *reserved;
        }
    }
    const int status = broadcast(&sequence, 1, MPI_UINT32_T, 0, context.communicator);
    if (status != MPI_SUCCESS) {
        invoke_mpi_abort(abort, context.communicator, status);
    }
    if (sequence == exhausted) {
        return std::unexpected(
            RunConfigurationError{.code = RunConfigurationErrorCode::id_space_exhausted,
                                  .message = "run identity sequence space is exhausted for MPI world rank " +
                                             std::to_string(context.world_origin)});
    }
    const auto origin = static_cast<std::uint32_t>(context.world_origin);
    return (static_cast<std::uint64_t>(origin) << 32U) | sequence;
}

std::expected<std::uint64_t, RunConfigurationError> reserve_collective_id(MonotonicIdAllocator& allocator,
                                                                          const MPI_Comm communicator,
                                                                          const CollectiveMaximum collective_maximum,
                                                                          const MpiAbort abort)
{
    const std::scoped_lock lock(allocator.mutex_);
    constexpr auto exhausted = std::numeric_limits<std::uint64_t>::max();
    const auto local_candidate = allocator.reserve_local_candidate_unlocked();
    std::uint64_t chosen = exhausted;
    const int status = collective_maximum(local_candidate, chosen, communicator);
    if (status != MPI_SUCCESS) {
        invoke_mpi_abort(abort, communicator, status);
    }
    if (chosen == exhausted) {
        return std::unexpected(RunConfigurationError{.code = RunConfigurationErrorCode::id_space_exhausted,
                                                     .message = "phase-graph identity space is exhausted"});
    }
    allocator.advance_beyond_unlocked(chosen);
    return chosen;
}

RunConfigurationControl::RunConfigurationControl(const MPI_Comm communicator, const RunConfigurationId id,
                                                 const std::uint64_t next_graph_id, const MpiAbort abort) noexcept :
    communicator_(communicator), id_(id), graph_ids_(next_graph_id), abort_(abort)
{
}

RunConfigurationControl::~RunConfigurationControl()
{
    release_communicator(communicator_, {.initialized = MPI_Initialized,
                                         .finalized = MPI_Finalized,
                                         .free_communicator = MPI_Comm_free,
                                         .abort = abort_});
}

std::expected<std::uint64_t, RunConfigurationError> RunConfigurationControl::reserve_graph_id() const
{
    return reserve_collective_id(graph_ids_, communicator_, mpi_collective_maximum, abort_);
}

std::expected<RunConfiguration, RunConfigurationError> create_run_configuration(const MPI_Comm communicator,
                                                                                const MpiRunOperations operations,
                                                                                RunSequenceAllocator& allocator)
{
    int initialized = 0;
    const int initialized_status = MPI_Initialized(&initialized);
    int finalized = 0;
    const int finalized_status = MPI_Finalized(&finalized);
    auto lifecycle = validate_mpi_lifecycle({.initialized_status = initialized_status,
                                             .initialized = initialized,
                                             .finalized_status = finalized_status,
                                             .finalized = finalized,
                                             .communicator = communicator},
                                            operations.abort);
    if (!lifecycle) {
        return std::unexpected(std::move(lifecycle).error());
    }

    auto communicator_kind =
        validate_intracommunicator(communicator, operations.test_intercommunicator, operations.abort);
    if (!communicator_kind) {
        return std::unexpected(std::move(communicator_kind).error());
    }

    auto rank_and_origin = communicator_rank_and_origin(communicator, operations);
    if (!rank_and_origin) {
        return std::unexpected(std::move(rank_and_origin).error());
    }

    const MPI_Comm duplicate = duplicate_communicator(communicator, operations.duplicate, operations.abort);
    auto id = reserve_run_id(allocator,
                             {.communicator = duplicate,
                              .communicator_rank = rank_and_origin->first,
                              .world_origin = rank_and_origin->second},
                             operations.broadcast, operations.abort);
    if (!id) {
        MPI_Comm owned = duplicate;
        const int free_status = operations.free_communicator(&owned);
        if (free_status != MPI_SUCCESS) {
            invoke_mpi_abort(operations.abort, communicator, free_status);
        }
        return std::unexpected(std::move(id).error());
    }

    std::shared_ptr<const RunConfigurationControl> control;
    try {
        control = operations.allocate_control(duplicate, RunConfigurationId::from_index(*id), operations.abort);
    }
    catch (const std::bad_alloc&) {
        invoke_mpi_abort(operations.abort, communicator, MPI_ERR_NO_MEM);
    }
    return RunConfigurationAccess::adopt(std::move(control));
}

} // namespace detail

RunConfiguration::RunConfiguration(std::shared_ptr<const detail::RunConfigurationControl> control) noexcept :
    control_(std::move(control))
{
}

std::expected<RunConfiguration, RunConfigurationError> RunConfiguration::create(const MPI_Comm communicator)
{
    return detail::create_run_configuration(
        communicator,
        detail::MpiRunOperations{.test_intercommunicator = MPI_Comm_test_inter,
                                 .communicator_rank = MPI_Comm_rank,
                                 .communicator_size = MPI_Comm_size,
                                 .allocate_rank_buffer = detail::allocate_rank_buffer,
                                 .communicator_group = MPI_Comm_group,
                                 .translate_ranks = MPI_Group_translate_ranks,
                                 .free_group = MPI_Group_free,
                                 .collective_maximum = mpi_collective_maximum,
                                 .duplicate = MPI_Comm_dup,
                                 .broadcast = MPI_Bcast,
                                 .free_communicator = MPI_Comm_free,
                                 .allocate_control = detail::allocate_run_control,
                                 .abort = MPI_Abort},
        run_sequence_allocator());
}

RunConfigurationId RunConfiguration::id() const noexcept { return control_->id(); }

MPI_Comm RunConfiguration::communicator() const noexcept { return control_->communicator(); }

} // namespace rift
