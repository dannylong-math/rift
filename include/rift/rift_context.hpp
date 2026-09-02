#pragma once

/**
 * \file
 * \brief Process-wide Rift simulation context.
 */

#include <deal.II/base/mpi.h>

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

private:
    /**
     * \brief RAII owner of deal.II, p4est, and MPI initialization state.
     */
    dealii::Utilities::MPI::MPI_InitFinalize mpi_lifetime_;
};

} // namespace rift
