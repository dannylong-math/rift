#pragma once

/**
 * \file
 * \brief Collective run identity and owned MPI communicator lifetime.
 */

#include <cstdint>
#include <expected>
#include <memory>
#include <mpi.h>
#include <rift/strong_id.hpp>
#include <string>

namespace rift {

/**
 * \defgroup run_configuration Run configuration
 * \brief Shared identity and communicator ownership for one solver run.
 */

namespace detail {

/**
 * \brief Distinguish run identities from every other strong identifier.
 *
 * Application code uses `RunConfigurationId`; this empty tag exists only to
 * provide compile-time identity.
 *
 * \code{.cpp}
 * using Id = rift::StrongId<rift::detail::RunConfigurationIdTag,
 *                           std::uint64_t>;
 * const Id id = Id::from_index(0);
 * \endcode
 *
 * \ingroup run_configuration
 */
struct RunConfigurationIdTag {};

/**
 * \brief Distinguish graph-instance identities from graph-local indices.
 *
 * Application code uses `PhaseGraphInstanceId`. The tag has no runtime state.
 *
 * \code{.cpp}
 * using Id = rift::StrongId<rift::detail::PhaseGraphInstanceIdTag,
 *                           std::uint64_t>;
 * const Id id = Id::from_index(0);
 * \endcode
 *
 * \ingroup run_configuration
 */
struct PhaseGraphInstanceIdTag {};

/**
 * \brief Own the duplicated communicator and monotonic graph-ID state.
 *
 * `RunConfiguration` and all derived run objects share this implementation
 * object through `std::shared_ptr<const RunConfigurationControl>`. Its stable
 * identity and communicator never change; only its internal ID allocator
 * advances during collective construction.
 *
 * \code{.cpp}
 * auto run = rift::RunConfiguration::create(MPI_COMM_WORLD).value();
 * auto graph = rift::make_phase_graph(run, {{"gas", "compressible"}}, {})
 *                  .value();
 * // Both handles retain the same private run control.
 * \endcode
 *
 * \ingroup run_configuration
 */
class RunConfigurationControl;

/**
 * \brief Give trusted factories access to a run's shared private control.
 *
 * Application code does not use this gateway. It lets graph and mesh
 * factories retain the run lifetime without exposing ownership machinery in
 * the public API.
 *
 * \code{.cpp}
 * // Inside a trusted factory:
 * const auto control =
 *     rift::detail::RunConfigurationAccess::control(run);
 * \endcode
 *
 * \ingroup run_configuration
 */
struct RunConfigurationAccess;

} // namespace detail

/**
 * \brief Identity never reused by another supported run in one MPI execution.
 * \ingroup run_configuration
 */
using RunConfigurationId = StrongId<detail::RunConfigurationIdTag, std::uint64_t>;

/**
 * \brief Never-reused identity of one graph construction within a run.
 * \ingroup run_configuration
 */
using PhaseGraphInstanceId = StrongId<detail::PhaseGraphInstanceIdTag, std::uint64_t>;

/**
 * \brief Classify a failure to establish shared run control.
 *
 * Use the code for programmatic recovery and the accompanying message for a
 * deterministic diagnostic.
 *
 * \ingroup run_configuration
 */
enum class RunConfigurationErrorCode : std::uint8_t {
    /** \brief MPI has not been initialized. */
    mpi_not_initialized,
    /** \brief MPI has already been finalized. */
    mpi_finalized,
    /** \brief The supplied communicator is `MPI_COMM_NULL`. */
    null_communicator,
    /** \brief Rift requires an intracommunicator for run-owned collective state. */
    intercommunicator_not_supported,
    /** \brief At least one communicator member is outside the current `MPI_COMM_WORLD`. */
    communicator_not_world_derived,
    /** \brief The finite strong-ID representation has been exhausted. */
    id_space_exhausted,
};

/**
 * \brief Report one run-configuration construction failure.
 *
 * \code{.cpp}
 * const auto run = rift::RunConfiguration::create(MPI_COMM_WORLD);
 * if (!run)
 *     std::cerr << run.error().message << '\n';
 * \endcode
 *
 * \ingroup run_configuration
 */
struct RunConfigurationError {
    /** \brief Machine-readable failure classification. */
    RunConfigurationErrorCode code;
    /** \brief Deterministic human-readable diagnostic. */
    std::string message;
};

/**
 * \brief Share one run identity and an owned duplicate of its MPI communicator.
 *
 * Create this handle once at the beginning of a run and pass it to collective
 * factories. The factory duplicates the communicator, so the caller may free
 * its original communicator immediately afterward. Copies, moved handles,
 * graphs, and later run-owned objects all share the same immutable control.
 * Release all run-owned objects before MPI finalization.
 *
 * \code{.cpp}
 * int main(int argc, char **argv) {
 *     MPI_Init(&argc, &argv);
 *     {
 *         auto run_result =
 *             rift::RunConfiguration::create(MPI_COMM_WORLD);
 *         if (!run_result) {
 *             std::cerr << run_result.error().message << '\n';
 *             MPI_Abort(MPI_COMM_WORLD, 1);
 *         }
 *
 *         rift::RunConfiguration run = std::move(*run_result);
 *         std::cout << "run " << run.id().value() << '\n';
 *         // Collective graph, mesh, and state factories receive `run` here.
 *     }
 *     MPI_Finalize();
 * }
 * \endcode
 *
 * `create()` accepts only intracommunicators and is collective over that
 * communicator. Copying, moving, querying, and destroying this handle perform
 * no run-state transition.
 *
 * \ingroup run_configuration
 */
class RunConfiguration {
public:
    /**
     * \brief Collectively create shared run control.
     *
     * Every rank in an intracommunicator must call this function in the same
     * collective order. Intercommunicators are rejected before communicator
     * duplication or ID allocation because Rift run, mesh, and state
     * collectives require one local group. The returned communicator is an
     * owned duplicate and the run ID agrees on all participating ranks.
     * Every communicator member must belong to the same `MPI_COMM_WORLD`.
     * Communicator rank zero's world rank and a finite per-origin sequence
     * form the run identity, which is never reused by another supported run
     * within the same MPI execution.
     * Intercommunicators and communicators from unrelated dynamic-process or
     * MPI Sessions process worlds are rejected.
     * MPI operation failures are fatal and invoke `MPI_Abort` because ranks
     * cannot safely recover independently from a failed collective operation.
     *
     * \param communicator live MPI intracommunicator defining the run.
     * \return shared run handle, or a deterministic construction error.
     */
    [[nodiscard]] static std::expected<RunConfiguration, RunConfigurationError> create(MPI_Comm communicator);

    /**
     * \brief Read the communicator-consistent run identity.
     * \return never-reused identity allocated by `create()`.
     */
    [[nodiscard]] RunConfigurationId id() const noexcept;

    /**
     * \brief Borrow the communicator owned by this run.
     *
     * The handle remains valid while this run or any derived run-owned object
     * retains the shared control. Do not free the returned communicator.
     *
     * \return borrowed duplicated communicator.
     */
    [[nodiscard]] MPI_Comm communicator() const noexcept;

private:
    /** \brief Permit trusted factories to retain the private shared control. */
    friend struct detail::RunConfigurationAccess;

    /**
     * \brief Adopt newly created shared control.
     * \param control validated run control to retain.
     */
    explicit RunConfiguration(std::shared_ptr<const detail::RunConfigurationControl> control) noexcept;

    /** \brief Shared immutable run identity, communicator, and ID allocator. */
    std::shared_ptr<const detail::RunConfigurationControl> control_;
};

} // namespace rift
