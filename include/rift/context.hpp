#pragma once

/**
 * \file
 * \brief Process-wide runtime ownership and shared Rift services.
 */

#include <deal.II/base/conditional_ostream.h>
#include <deal.II/base/enable_observer_pointer.h>
#include <deal.II/base/exceptions.h>
#include <deal.II/base/logstream.h>
#include <deal.II/base/mpi.h>
#include <deal.II/base/timer.h>
#include <iostream>
#include <memory>

namespace rift {

template<int dim, typename Number> class PhaseCatalog;

/**
 * \brief Own the process-wide Rift runtime services for one simulation.
 *
 * Context initializes and finalizes MPI, stores the communicator used by Rift,
 * and owns shared logging and timing services. Construct one near the beginning
 * of main() and keep it alive until every Rift object that borrows it has been
 * destroyed. MPI must not already be initialized when the Context is created.
 *
 * Context is noncopyable and nonmovable, giving
 * [dealii::ObserverPointer](https://dealii.org/9.8.0/doxygen/deal.II/classObserverPointer.html)
 * observers a stable address. Its object identity defines which Rift objects
 * belong to the same simulation; separately constructed contexts are distinct
 * even if they use the same MPI communicator. Exactly one PhaseCatalog may
 * claim a Context during its lifetime.
 *
 * \par Typical use
 * \code{.cpp}
 * int main(int argc, char** argv) {
 *   rift::Context context(argc, argv);
 *   context.pcout() << "Running on " << context.n_mpi_processes()
 *                   << " MPI ranks\n";
 *
 *   // Construct objects that borrow context here.
 *   // They are destroyed before context finalizes MPI.
 * }
 * \endcode
 */
class Context : public dealii::EnableObserverPointer {
public:
    /**
     * \brief Initialize MPI and construct context services.
     * \param argc Argument count passed through to MPI initialization.
     * \param argv Argument vector passed through to MPI initialization.
     * \param mpi_comm Borrowed communicator, normally MPI_COMM_WORLD.
     *
     * \throws dealii::ExceptionBase If MPI has already been initialized
     * externally. That existing MPI session remains owned by the caller.
     */
    Context(int& argc, char**& argv, MPI_Comm mpi_comm = MPI_COMM_WORLD) :
        mpi_init_finalize_(argc, argv),
        mpi_comm_(mpi_comm),
        pcout_(std::cout, this_mpi_process() == 0),
        timer_(mpi_comm_, pcout_, dealii::TimerOutput::never, dealii::TimerOutput::wall_times)
    {
    }

    /** \brief Context has one owner and cannot be copied. */
    Context(const Context&) = delete;
    /** \brief Context ownership cannot be replaced by copying. */
    Context& operator=(const Context&) = delete;
    /** \brief Keep the address and identity stable for all observers. */
    Context(Context&&) = delete;
    /** \brief Context ownership cannot be replaced by moving. */
    Context& operator=(Context&&) = delete;
    /** \brief Destroy the owned services and finalize the Context-owned MPI session. */
    ~Context() override = default;

    /**
     * \brief Return the pointer identity of this live context in this process.
     * \return The stable address of this nonmovable object. Equal IDs identify
     * the same live Context; the pointer does not extend its lifetime.
     *
     * Compare IDs only within one process while both contexts are alive.
     * The value is not a cross-rank or persistent identifier; an address may
     * be reused after destruction.
     */
    [[nodiscard]] const Context* id() const noexcept { return std::addressof(*this); }

    /**
     * \brief Return this process's rank in the context communicator.
     * \return Zero-based communicator rank.
     */
    [[nodiscard]] unsigned int this_mpi_process() const { return dealii::Utilities::MPI::this_mpi_process(mpi_comm_); }

    /**
     * \brief Borrow the communicator used by Rift services.
     * \return Communicator whose ownership remains with the caller or MPI.
     *
     * \warning The returned communicator is borrowed. Callers must not free it
     * through this handle.
     */
    [[nodiscard]] MPI_Comm mpi_comm() const noexcept { return mpi_comm_; }

    /**
     * \brief Return the number of processes in the context communicator.
     * \return Communicator size.
     */
    [[nodiscard]] unsigned int n_mpi_processes() const { return dealii::Utilities::MPI::n_mpi_processes(mpi_comm_); }

    /**
     * \brief Borrow the context-owned
     * [dealii::LogStream](https://dealii.org/9.8.0/doxygen/deal.II/classLogStream.html)
     * with deal.II's default setup.
     * \return Mutable stream reference that must not outlive this Context.
     */
    [[nodiscard]] dealii::LogStream& log_stream() noexcept { return log_stream_; }

    /**
     * \brief Borrow
     * [dealii::ConditionalOStream](https://dealii.org/9.8.0/doxygen/deal.II/classConditionalOStream.html)
     * output enabled only on communicator rank zero.
     * \return Mutable conditional stream that must not outlive this Context.
     */
    [[nodiscard]] dealii::ConditionalOStream& pcout() noexcept { return pcout_; }

    /**
     * \brief Borrow the context-owned wall-time timer with automatic output disabled.
     *
     * Timed sections synchronize over mpi_comm() and require matching calls
     * across ranks. Use
     * [dealii::TimerOutput::Scope](https://dealii.org/9.8.0/doxygen/deal.II/classTimerOutput_1_1Scope.html)
     * to close sections before teardown. The timer must not be used
     * concurrently by multiple threads. Borrowed service references and timer
     * scopes must not outlive this context.
     *
     * \return Mutable timer reference owned by this Context.
     */
    [[nodiscard]] dealii::TimerOutput& timer() noexcept { return timer_; }

private:
    /**
     * \brief Permanently grant this Context's sole phase-catalog claim.
     * \throws dealii::ExceptionBase If a catalog has already claimed this
     * Context, including a catalog that has since been destroyed.
     *
     * Catalog construction is a serialized configuration operation. This
     * function does not synchronize concurrent callers.
     */
    void claim_phase_catalog()
    {
        AssertThrow(!phase_catalog_claimed_,
                    dealii::ExcMessage("This Context has already been claimed by a PhaseCatalog."));
        phase_catalog_claimed_ = true;
    }

    template<int dim, typename Number> friend class PhaseCatalog;

    /** \brief Initialize MPI first and finalize it after all other services are destroyed. */
    dealii::Utilities::MPI::MPI_InitFinalize mpi_init_finalize_;
    /** \brief Borrowed communicator used for rank queries and collective timing. */
    MPI_Comm mpi_comm_;

    /** \brief Centralized log stream. */
    dealii::LogStream log_stream_;
    /** \brief std::cout wrapper enabled only on communicator rank zero. */
    dealii::ConditionalOStream pcout_;
    /** \brief Collective wall-time measurements. */
    dealii::TimerOutput timer_;

    /** \brief Whether this Context has permanently granted its catalog claim. */
    bool phase_catalog_claimed_{false};
};

} // namespace rift
