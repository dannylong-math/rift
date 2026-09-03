#pragma once

/**
 * \file
 * \brief Process-wide Rift simulation context.
 */

#include <cstdint>
#include <deal.II/base/mpi.h>
#include <memory>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/phase_support.hpp>
#include <vector>

namespace rift {

/**
 * \brief Own process-wide Rift services and their runtime lifetime.
 *
 * Create exactly one instance on every rank near the top of `main()`.
 * This object handles all of the MPI setup required by external libraries.
 * It also configures the thread pool size for parallel mesh algorithms.
 * This object must outlive every other object that depends on deal.II, p4est, or MPI.
 *
 * ```cpp
 * #include <rift/rift_context.hpp>
 * int main(int argc, char** argv)
 * {
 *     rift::RiftContext context(argc, argv);
 *     // ... rest of the program
 *     return 0; // Destruction of `context` finalizes deal.II, p4est, and MPI.
 * }
 * ```
 *
 * \ingroup core
 */
class RiftContext {
public:
    /**
     * \brief Initialize the process-wide runtime.
     *
     * \param[in,out] argc command-line argument count forwarded to deal.II and
     * MPI; initialization may update it.
     * \param[in,out] argv command-line arguments forwarded to deal.II and MPI;
     * initialization may update them.
     * \param[in] max_threads maximum thread count forwarded unchanged to
     * deal.II.
     */
    RiftContext(int& argc, char**& argv, const unsigned int max_threads = 1) : mpi_lifetime_(argc, argv, max_threads) {}

    /**
     * \brief Destructor. Finalizes the process-wide runtime.
     */
    ~RiftContext() = default;

    /**
     * \brief Copy construction is disabled because the runtime has one owner.
     */
    RiftContext(const RiftContext&) = delete;

    /**
     * \brief Copy assignment is disabled because the runtime has one owner.
     */
    RiftContext& operator=(const RiftContext&) = delete;

    /**
     * \brief Move construction is disabled to keep the lifetime anchor stable.
     */
    RiftContext(RiftContext&&) = delete;

    /**
     * \brief Move assignment is disabled to keep the lifetime anchor stable.
     */
    RiftContext& operator=(RiftContext&&) = delete;

    /**
     * \brief Access Rift's world communicator during this context's
     * lifetime.
     *
     * \return borrowed `MPI_COMM_WORLD`; the caller must never free it.
     */
    // The live context makes the MPI-lifetime precondition explicit.
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    [[nodiscard]] MPI_Comm mpi_communicator() const noexcept { return MPI_COMM_WORLD; }

    /**
     * \brief Query the calling rank in Rift's world communicator.
     *
     * \return zero-based world rank in `[0, n_mpi_processes())`.
     */
    [[nodiscard]] unsigned int this_mpi_process() const
    {
        return dealii::Utilities::MPI::this_mpi_process(mpi_communicator());
    }

    /**
     * \brief Query the number of ranks in Rift's world communicator.
     *
     * \return positive world-communicator size.
     */
    [[nodiscard]] unsigned int n_mpi_processes() const
    {
        return dealii::Utilities::MPI::n_mpi_processes(mpi_communicator());
    }

    /**
     * \brief Collectively create and own this context's canonical phase graph.
     *
     * Every rank in `mpi_communicator()` must call this member in the same
     * order. The first call consumes the context's only graph-creation attempt,
     * whether it succeeds or returns configuration errors.
     *
     * \param specification unresolved phase and interface input owned by this
     * call.
     * \param compatibility_test local compatibility policy invoked in
     * canonical interface order after structural agreement.
     * \return a borrowed reference to the context-owned immutable graph, or
     * collected coherent errors on every participating rank.
     */
    [[nodiscard]] PhaseGraphResult create_phase_graph(PhaseGraphSpecification specification,
                                                      InterfaceCompatibilityTest compatibility_test);

    /**
     * \brief Collectively create an immutable distributed mesh snapshot.
     *
     * Every world rank must call this member in the same order with the same
     * supported dimension. The call consumes both resource pointers on every
     * result and publishes no snapshot when any rank supplies invalid input.
     *
     * \tparam dim volume-mesh dimension; only 2 and 3 are supported.
     * \param triangulation completed distributed triangulation to adopt.
     * \param mapping object describing polynomial mapping from reference to physical cells.
     * \return a shared immutable snapshot or coherent ordered errors on every
     * rank.
     */
    template<int dim>
        requires(dim == 2 || dim == 3)
    [[nodiscard]] MeshSnapshotResult<dim>
    create_mesh_snapshot(std::unique_ptr<dealii::parallel::distributed::Triangulation<dim>> triangulation,
                         std::unique_ptr<dealii::Mapping<dim>> mapping);

    /**
     * \brief Collectively create immutable conforming support for every phase.
     *
     * Every world rank must call this member in the same order with the same
     * supported dimension and logical mesh snapshot. Each rank supplies
     * exactly one specification per canonical phase, containing only requested
     * locally owned active cells.
     *
     * \tparam dim volume-mesh dimension; only 2 and 3 are supported.
     * \param mesh immutable mesh snapshot retained by a successful result.
     * \param specifications owner-local requests owned by this call.
     * \return a complete immutable support aggregate or coherent ordered
     * errors on every rank.
     */
    template<int dim>
        requires(dim == 2 || dim == 3)
    [[nodiscard]] PhaseSupportResult<dim> create_phase_supports(std::shared_ptr<const MeshSnapshot<dim>> mesh,
                                                                std::vector<PhaseSupportSpecification> specifications);

private:
    /**
     * \brief RAII owner of deal.II, p4est, and MPI initialization state.
     */
    dealii::Utilities::MPI::MPI_InitFinalize mpi_lifetime_;

    /** \brief Whether the context's single graph-creation attempt was consumed. */
    bool phase_graph_creation_attempted_ = false;

    /** \brief Canonical graph destroyed before the MPI lifetime ends. */
    std::unique_ptr<const PhaseGraph> phase_graph_;

    /** \brief Next context-local identity for a successfully published mesh. */
    std::uint64_t next_mesh_snapshot_index_ = 0;
};

} // namespace rift
