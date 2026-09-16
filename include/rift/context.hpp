#pragma once

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
 * \brief Owns MPI lifetime, logging, timing, and the phase-catalog claim.
 *
 * A `Context` is an object with useful utility services that are used throughout
 * the library. The intent is to create one `Context` at the beginning of `main()`
 * and have other objects (internally) borrow it. The `Context` is noncopyable and
 * nonmovable, so dependents may retain `dealii::ObserverPointer<Context>` in order
 * access these utilities.
 *
 * `Context` is treated as unique even if multiple instances have the same underlying values
 * within the object. This means creating two different `Context` objects with the same inputs
 * will cause interoperability issues with the other Rift objects.
 *
 * ```cpp
 * int main(int argc, char** argv) {
 *     // Create context initializes MPI and other utilities.
 *     rift::Context context(argc, argv);
 *     // ... rest of your program ...
 *     return 0;
 *     // Since context is the first object created in main, it will be destroyed last.
 *     // Furthermore, it is setup so that MPI is finalized last as well.
 * }
 * ```
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
    /** \brief Destruct the context and finalize MPI. */
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

    /** \brief Return this process's rank in the context communicator. */
    [[nodiscard]] unsigned int this_mpi_process() const { return dealii::Utilities::MPI::this_mpi_process(mpi_comm_); }

    /** \brief Return the borrowed communicator; callers must not free it. */
    [[nodiscard]] MPI_Comm mpi_comm() const noexcept { return mpi_comm_; }

    /** \brief Return the number of processes in the context communicator. */
    [[nodiscard]] unsigned int n_mpi_processes() const { return dealii::Utilities::MPI::n_mpi_processes(mpi_comm_); }

    /** \brief Borrow the context-owned log stream with deal.II's default setup. */
    [[nodiscard]] dealii::LogStream& log_stream() noexcept { return log_stream_; }

    /** \brief Borrow std::cout output enabled only on communicator rank zero. */
    [[nodiscard]] dealii::ConditionalOStream& pcout() noexcept { return pcout_; }

    /**
     * \brief Borrow the context-owned wall-time timer with automatic output disabled.
     *
     * Timed sections synchronize over mpi_comm() and require matching calls
     * across ranks. Use TimerOutput::Scope to close sections before teardown.
     * The timer must not be used concurrently by multiple threads. Borrowed
     * service references and timer scopes must not outlive this context.
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
