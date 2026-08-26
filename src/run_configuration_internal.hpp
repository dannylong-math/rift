#pragma once

/**
 * \file
 * \brief Private shared control used by run-owned Rift objects.
 */

#include <cstdint>
#include <expected>
#include <memory>
#include <mpi.h>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <mutex>
#include <rift/run_configuration.hpp>
#include <utility>
#include <vector>

namespace rift::detail {

/** \brief MPI signature used to inject communicator duplication in private tests. */
using MpiCommDup = int (*)(MPI_Comm, MPI_Comm*);

/** \brief MPI signature used to inject communicator-kind queries in private tests. */
using MpiCommTestInter = int (*)(MPI_Comm, int*);

/** \brief MPI signature used to inject communicator-rank queries in private tests. */
using MpiCommRank = int (*)(MPI_Comm, int*);

/** \brief MPI signature used to inject communicator-size queries in private tests. */
using MpiCommSize = int (*)(MPI_Comm, int*);

/** \brief MPI signature used to inject communicator-group queries in private tests. */
using MpiCommGroup = int (*)(MPI_Comm, MPI_Group*);

/** \brief MPI signature used to inject group-rank translation in private tests. */
using MpiGroupTranslateRanks = int (*)(MPI_Group, int, const int*, MPI_Group, int*);

/** \brief MPI signature used to inject group cleanup in private tests. */
using MpiGroupFree = int (*)(MPI_Group*);

/** \brief MPI signature used to inject communicator broadcast in private tests. */
using MpiBroadcast = int (*)(void*, int, MPI_Datatype, int, MPI_Comm);

/** \brief MPI signature used to inject communicator cleanup in private tests. */
using MpiCommFree = int (*)(MPI_Comm*);

/** \brief MPI fatal handler used after an operation failure prevents safe recovery. */
using MpiAbort = int (*)(MPI_Comm, int);

/** \brief MPI signature used to inject initialization and finalization queries. */
using MpiLifecycleQuery = int (*)(int*);

/** \brief Private allocation seam for shared run control. */
using RunControlAllocator = std::shared_ptr<const RunConfigurationControl> (*)(MPI_Comm, RunConfigurationId, MpiAbort);

/** \brief Private allocation seam for one rank-translation buffer. */
using RankBufferAllocator = std::vector<int> (*)(int);

/** \brief Narrow collective-maximum signature used by the ID allocator. */
using CollectiveMaximum = int (*)(std::uint64_t, std::uint64_t&, MPI_Comm);

/**
 * \brief Allocate the finite per-origin sequence encoded in run identities.
 *
 * Only communicator rank zero accesses its process-local allocator. The
 * selected sequence is then broadcast to the complete intracommunicator.
 * `uint32_t` maximum is an exhaustion sentinel and is never issued.
 *
 * \code{.cpp}
 * rift::detail::RunSequenceAllocator sequences;
 * const auto first = sequences.reserve().value();
 * \endcode
 */
class RunSequenceAllocator {
public:
    /**
     * \brief Select the first sequence representation considered unreserved.
     * \param next_sequence initial sequence, normally zero.
     */
    explicit RunSequenceAllocator(std::uint64_t next_sequence = 0) noexcept : next_sequence_(next_sequence) {}

    /**
     * \brief Reserve the next finite sequence without wrapping.
     * \return sequence, or `id_space_exhausted` at the reserved sentinel.
     */
    [[nodiscard]] std::expected<std::uint32_t, RunConfigurationError> reserve() noexcept;

private:
    /** \brief Protect the process-local sequence counter. */
    std::mutex mutex_;
    /** \brief First sequence representation not yet consumed. */
    std::uint64_t next_sequence_;
};

/**
 * \brief Allocate finite integer identities monotonically without wraparound.
 *
 * The mutex protects the process-local counter. Applications must still enter
 * collective graph construction in the same order on every rank; this class
 * does not define or repair cross-rank thread ordering. The maximum
 * representation is retained as an exhaustion sentinel and is never issued.
 *
 * \code{.cpp}
 * rift::detail::MonotonicIdAllocator allocator;
 * const std::uint64_t first = allocator.reserve_local_candidate();
 * allocator.advance_beyond(first);
 * \endcode
 */
class MonotonicIdAllocator {
public:
    /**
     * \brief Select the first local candidate.
     * \param next_candidate initial unreserved representation.
     */
    explicit MonotonicIdAllocator(std::uint64_t next_candidate = 0) noexcept : next_candidate_(next_candidate) {}

    /**
     * \brief Reserve one local candidate without wrapping.
     * \return candidate, or `uint64_t` maximum when exhausted.
     */
    [[nodiscard]] std::uint64_t reserve_local_candidate() noexcept;

    /**
     * \brief Ensure every representation through `chosen` remains consumed.
     * \param chosen communicator-wide candidate to advance beyond.
     */
    void advance_beyond(std::uint64_t chosen) noexcept;

private:
    /** \brief Reserve one candidate while the caller holds `mutex_`. */
    [[nodiscard]] std::uint64_t reserve_local_candidate_unlocked() noexcept;

    /** \brief Consume representations through `chosen` while the caller holds `mutex_`. */
    void advance_beyond_unlocked(std::uint64_t chosen) noexcept;

    /** \brief Serialize the complete local-reserve/collective/advance transaction. */
    friend std::expected<std::uint64_t, RunConfigurationError>
    reserve_collective_id(MonotonicIdAllocator& allocator, MPI_Comm communicator, CollectiveMaximum collective_maximum,
                          MpiAbort abort);

    /** \brief Serialize access to the finite monotonic counter. */
    std::mutex mutex_;
    /** \brief First local representation not yet consumed. */
    std::uint64_t next_candidate_;
};

/**
 * \brief Collect the MPI calls used by run construction.
 *
 * Production passes the real MPI routines. Explicit function pointers let
 * deterministic private tests exercise MPI error translation without invoking
 * undefined behavior on invalid communicators.
 */
struct MpiRunOperations {
    /** \brief Local communicator-kind query routine. */
    MpiCommTestInter test_intercommunicator;
    /** \brief Local communicator-rank query routine. */
    MpiCommRank communicator_rank;
    /** \brief Local communicator-size query routine. */
    MpiCommSize communicator_size;
    /** \brief Rank-translation buffer allocation routine. */
    RankBufferAllocator allocate_rank_buffer;
    /** \brief Local communicator-group query routine. */
    MpiCommGroup communicator_group;
    /** \brief Local group-rank translation routine. */
    MpiGroupTranslateRanks translate_ranks;
    /** \brief Local group cleanup routine. */
    MpiGroupFree free_group;
    /** \brief Communicator-wide maximum used to agree on origin support. */
    CollectiveMaximum collective_maximum;
    /** \brief Communicator duplication routine. */
    MpiCommDup duplicate;
    /** \brief Communicator broadcast routine. */
    MpiBroadcast broadcast;
    /** \brief Communicator cleanup routine. */
    MpiCommFree free_communicator;
    /** \brief Shared run-control allocation routine. */
    RunControlAllocator allocate_control;
    /** \brief Fatal operation used when ranks cannot safely rejoin. */
    MpiAbort abort;
};

/**
 * \brief Collect the noncollective MPI operations used during run cleanup.
 *
 * Production uses MPI lifecycle queries and communicator cleanup directly.
 * Explicit operations let private tests verify fatal status handling without
 * corrupting a real MPI communicator.
 *
 * \code{.cpp}
 * rift::detail::MpiCleanupOperations operations{
 *     .initialized = MPI_Initialized,
 *     .finalized = MPI_Finalized,
 *     .free_communicator = MPI_Comm_free,
 *     .abort = MPI_Abort,
 * };
 * \endcode
 */
struct MpiCleanupOperations {
    /** \brief MPI initialization query. */
    MpiLifecycleQuery initialized;
    /** \brief MPI finalization query. */
    MpiLifecycleQuery finalized;
    /** \brief Owned communicator cleanup. */
    MpiCommFree free_communicator;
    /** \brief Fatal operation for an MPI status failure. */
    MpiAbort abort;
};

/**
 * \brief Bind the communicator and origin fields used to encode a run ID.
 *
 * Keeping these related integers named prevents accidental exchange on MPI
 * implementations whose communicator handles are integer aliases.
 *
 * \code{.cpp}
 * const rift::detail::RunIdentityContext context{
 *     .communicator = MPI_COMM_WORLD,
 *     .communicator_rank = 0,
 *     .world_origin = 0,
 * };
 * \endcode
 */
struct RunIdentityContext {
    /** \brief Owned intracommunicator used to broadcast the sequence. */
    MPI_Comm communicator;
    /** \brief This process's rank in `communicator`. */
    int communicator_rank;
    /** \brief Communicator rank zero translated into `MPI_COMM_WORLD`. */
    int world_origin;
};

/**
 * \brief Capture MPI lifecycle results without swappable positional arguments.
 */
struct MpiLifecycleState {
    /** \brief Return status from `MPI_Initialized`. */
    int initialized_status;
    /** \brief Value returned by `MPI_Initialized`. */
    int initialized;
    /** \brief Return status from `MPI_Finalized`. */
    int finalized_status;
    /** \brief Value returned by `MPI_Finalized`. */
    int finalized;
    /** \brief Communicator supplied by the caller. */
    MPI_Comm communicator;
};

/**
 * \brief Translate MPI lifecycle query results into a run-construction result.
 * \param state query statuses, query values, and caller communicator.
 * \param abort fatal handler invoked when an MPI query returns failure.
 * \return success, or a deterministic logical lifecycle/input error.
 */
[[nodiscard]] std::expected<void, RunConfigurationError> validate_mpi_lifecycle(MpiLifecycleState state,
                                                                                MpiAbort abort);

/**
 * \brief Require a valid intracommunicator before entering Rift collectives.
 *
 * Query the caller's live handle before duplication. `MPI_Comm_test_inter` is
 * a local inquiry, whereas duplicating an unsupported intercommunicator is a
 * collective allocation that Rift neither needs nor can safely follow with
 * its intracommunicator-only identity reduction.
 *
 * \param communicator caller-owned communicator to inspect.
 * \param test_intercommunicator MPI-compatible communicator-kind query.
 * \param abort fatal handler invoked if the MPI inquiry itself fails.
 * \return success for an intracommunicator, or a deterministic type error.
 */
[[nodiscard]] std::expected<void, RunConfigurationError>
validate_intracommunicator(MPI_Comm communicator, MpiCommTestInter test_intercommunicator, MpiAbort abort);

/**
 * \brief Validate every communicator member and identify its world-rank origin.
 *
 * The translation is a local group operation on every participant. Every
 * participant translates the complete communicator group and reduces its
 * local support result before any rank returns. A communicator spanning
 * unrelated process worlds is therefore rejected collectively without
 * mismatching the later duplication or ID broadcast. `MPI_UNDEFINED` means a
 * member is not part of the initialized job's `MPI_COMM_WORLD`. MPI operation
 * failures invoke the fatal handler.
 *
 * \param communicator validated intracommunicator.
 * \param operations injected MPI operations and fatal handler.
 * \return pair of local rank and world-rank origin, or unsupported provenance.
 */
[[nodiscard]] std::expected<std::pair<int, int>, RunConfigurationError>
communicator_rank_and_origin(MPI_Comm communicator, const MpiRunOperations& operations);

/**
 * \brief Allocate one value-initialized rank-translation buffer.
 * \param size number of communicator ranks represented by the buffer.
 * \return buffer containing `size` integer entries.
 * \throws std::bad_alloc when local storage cannot be allocated.
 */
[[nodiscard]] std::vector<int> allocate_rank_buffer(int size);

/**
 * \brief Release an owned communicator while MPI remains active.
 *
 * Logical uninitialized or finalized states skip cleanup because MPI calls are
 * unavailable. A failed lifecycle query or communicator free invokes the
 * fatal handler and does not return.
 *
 * \param communicator owned communicator, set to null by successful MPI cleanup.
 * \param operations injected lifecycle, cleanup, and fatal operations.
 */
void release_communicator(MPI_Comm& communicator, MpiCleanupOperations operations);

/**
 * \brief Allocate shared run control after collective construction succeeds.
 * \param communicator owned duplicated communicator.
 * \param id allocated communicator-consistent run identity.
 * \param abort fatal handler retained by the control.
 * \return shared immutable control.
 * \throws std::bad_alloc when local control allocation fails.
 */
[[nodiscard]] std::shared_ptr<const RunConfigurationControl>
allocate_run_control(MPI_Comm communicator, RunConfigurationId id, MpiAbort abort);

/**
 * \brief Duplicate a communicator and translate failure deterministically.
 * \param communicator source communicator.
 * \param duplicate MPI-compatible duplication operation.
 * \param abort fatal handler invoked if duplication fails.
 * \return owned duplicate. Operation failure does not return.
 */
[[nodiscard]] MPI_Comm duplicate_communicator(MPI_Comm communicator, MpiCommDup duplicate, MpiAbort abort);

/**
 * \brief Allocate and encode one collision-free run identity within an MPI job.
 *
 * The upper 32 bits store communicator rank zero translated into
 * `MPI_COMM_WORLD`; the lower 32 bits store its never-reused local sequence.
 * Sequence maximum is reserved as a collectively propagated exhaustion
 * sentinel.
 *
 * \param allocator process-local allocator used only by communicator rank zero.
 * \param context owned communicator, local rank, and world-rank origin.
 * \param broadcast injected communicator broadcast.
 * \param abort fatal handler invoked if broadcast fails.
 * \return encoded identity, or collective finite-space exhaustion.
 */
[[nodiscard]] std::expected<std::uint64_t, RunConfigurationError>
reserve_run_id(RunSequenceAllocator& allocator, RunIdentityContext context, MpiBroadcast broadcast, MpiAbort abort);

/**
 * \brief Agree collectively on an identity while exposing deterministic fault injection.
 * \param allocator process-local monotonic allocator.
 * \param communicator communicator defining the collective.
 * \param collective_maximum collective maximum operation.
 * \param abort fatal handler invoked if the collective fails.
 * \return common numeric identity, or finite-space exhaustion; MPI failure is fatal.
 */
[[nodiscard]] std::expected<std::uint64_t, RunConfigurationError>
reserve_collective_id(MonotonicIdAllocator& allocator, MPI_Comm communicator, CollectiveMaximum collective_maximum,
                      MpiAbort abort);

/**
 * \brief Own a run communicator and reserve graph IDs collectively.
 *
 * This implementation type is shared as const. Its stable fields never
 * change; the mutable allocator advances only at collective graph
 * construction boundaries.
 *
 * \code{.cpp}
 * const auto control = RunConfigurationAccess::control(run);
 * const auto next_graph = control->reserve_graph_id();
 * \endcode
 */
class RunConfigurationControl {
public:
    /**
     * \brief Adopt a duplicated communicator and its allocated run identity.
     * \param communicator owned MPI communicator.
     * \param id communicator-consistent run identity.
     * \param next_graph_id initial unreserved graph identity, normally zero.
     * \param abort fatal handler for graph-identity collective failure.
     */
    RunConfigurationControl(MPI_Comm communicator, RunConfigurationId id, std::uint64_t next_graph_id = 0,
                            MpiAbort abort = MPI_Abort) noexcept;

    /**
     * \brief Release the owned communicator while MPI remains active.
     *
     * No run-state transition is performed. If process shutdown has already
     * finalized MPI, MPI owns the remaining communicator resource. MPI query
     * and cleanup status failures invoke the retained fatal handler.
     */
    ~RunConfigurationControl();

    RunConfigurationControl(const RunConfigurationControl&) = delete;
    RunConfigurationControl(RunConfigurationControl&&) = delete;
    RunConfigurationControl& operator=(const RunConfigurationControl&) = delete;
    RunConfigurationControl& operator=(RunConfigurationControl&&) = delete;

    /** \brief Read the stable run identity. */
    [[nodiscard]] RunConfigurationId id() const noexcept { return id_; }

    /** \brief Borrow the duplicated communicator. */
    [[nodiscard]] MPI_Comm communicator() const noexcept { return communicator_; }

    /**
     * \brief Borrow the fatal handler for another run-owned collective.
     * \return MPI-compatible operation retained when the run was created.
     */
    [[nodiscard]] MpiAbort abort_handler() const noexcept { return abort_; }

    /**
     * \brief Collectively reserve the next graph-instance representation.
     * \return common numeric identity, or a run-control error.
     */
    [[nodiscard]] std::expected<std::uint64_t, RunConfigurationError> reserve_graph_id() const;

private:
    /** \brief Duplicated communicator owned until control destruction. */
    MPI_Comm communicator_;
    /** \brief Stable run identity. */
    RunConfigurationId id_;
    /** \brief First graph-instance representation not yet reserved locally. */
    mutable MonotonicIdAllocator graph_ids_;
    /** \brief Fatal handler for irrecoverable graph-identity collectives. */
    MpiAbort abort_;
};

/**
 * \brief Access a run handle's private control from trusted factories.
 *
 * \code{.cpp}
 * const auto control = RunConfigurationAccess::control(run);
 * \endcode
 */
struct RunConfigurationAccess {
    /**
     * \brief Retain the control referenced by a public run handle.
     * \param run source handle.
     * \return shared immutable control.
     */
    [[nodiscard]] static std::shared_ptr<const RunConfigurationControl> control(const RunConfiguration& run) noexcept
    {
        return run.control_;
    }

    /**
     * \brief Adopt explicit control in another trusted factory or private test.
     * \param control shared control to retain.
     * \return public immutable run handle.
     */
    [[nodiscard]] static RunConfiguration adopt(std::shared_ptr<const RunConfigurationControl> control) noexcept
    {
        return RunConfiguration(std::move(control));
    }
};

/**
 * \brief Implement run creation with explicit private MPI dependencies.
 *
 * Public construction supplies the real MPI functions. Private tests may pass
 * deterministic failure operations to verify cleanup and fatal routing. A
 * local control-allocation failure invokes the fatal handler immediately:
 * communicator cleanup is collective and cannot be entered by only the rank
 * that failed allocation.
 *
 * \param communicator live collective communicator.
 * \param operations communicator topology, allocation, cleanup, and fatal operations.
 * \param allocator process-local per-origin run-sequence allocator.
 * \return shared run handle, or a deterministic construction error.
 */
[[nodiscard]] std::expected<RunConfiguration, RunConfigurationError>
create_run_configuration(MPI_Comm communicator, MpiRunOperations operations, RunSequenceAllocator& allocator);

} // namespace rift::detail
