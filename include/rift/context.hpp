#pragma once

#include <cstddef>
#include <deal.II/base/conditional_ostream.h>
#include <deal.II/base/logstream.h>
#include <deal.II/base/mpi.h>
#include <deal.II/base/timer.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <utility>

namespace rift {

class Context;

namespace detail {
class ContextImpl;

/**
 * \brief Access shared services from inside Rift.
 * \param context A valid, non-moved-from handle.
 * \return Implementation borrowed for the lifetime of its owning handles.
 */
[[nodiscard]] inline ContextImpl& context_impl(const Context& context) noexcept;
} // namespace detail

/**
 * \brief Create the shared services used by Rift objects.
 * \param argc Argument count from main; initialization may modify it.
 * \param argv Argument vector from main; initialization may modify it.
 * \param mpi_comm Borrowed communicator, normally MPI_COMM_WORLD. It must be
 * valid after MPI initialization and outlive all copies of the handle.
 * \return A handle owning initialized MPI, logging, and timing services.
 *
 * Call once at the beginning of main on every MPI rank. Keep this handle alive
 * until all dependent objects and worker activity have finished. The last
 * handle must be destroyed on the initializing thread. Initialization and
 * allocation failures propagate from deal.II and the standard library.
 */
[[nodiscard]] inline Context make_context(int& argc, char**& argv, MPI_Comm mpi_comm = MPI_COMM_WORLD);

/**
 * \brief Small shared-ownership handle passed to Rift objects.
 *
 * Obtain a handle through make_context(). Copies share the same services and
 * local phase counter. Store the handle by value in dependent objects.
 * A moved-from handle may only be destroyed or reassigned. Shared ownership
 * does not make MPI, output, or timing operations safe for concurrent use.
 */
class Context {
private:
    /**
     * \brief Take shared ownership of initialized context services.
     * \param impl Non-null implementation created by make_context().
     */
    explicit Context(std::shared_ptr<detail::ContextImpl> impl) noexcept : impl_(std::move(impl)) {}

    /** \brief Shared services; empty only after this handle has been moved from. */
    std::shared_ptr<detail::ContextImpl> impl_;

    /** \brief Allow the factory to construct a handle around initialized services. */
    friend Context make_context(int& argc, char**& argv, MPI_Comm mpi_comm);
    /** \brief Allow Rift's internal accessor to borrow the shared implementation. */
    friend detail::ContextImpl& detail::context_impl(const Context& context) noexcept;
};

namespace detail {

/** \brief Services shared by all handles to one context on this MPI rank. */
class ContextImpl {
public:
    /**
     * \brief Allocate the next zero-based local phase index.
     * \return The next index, starting at zero. The number of registrations
     * must remain representable by std::size_t.
     *
     * Registration is protected by a mutex and performs no MPI communication.
     * Different ranks may register different phases in different orders.
     * Indices are never recycled.
     */
    [[nodiscard]] std::size_t register_phase()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return n_phases_registered_++;
    }

    /** \brief Return this process's rank in the context communicator. */
    [[nodiscard]] unsigned int this_mpi_process() const { return dealii::Utilities::MPI::this_mpi_process(mpi_comm_); }

    /** \brief Return the borrowed communicator; callers must not free it. */
    [[nodiscard]] MPI_Comm mpi_comm() const noexcept { return mpi_comm_; }

    /** \brief Return the number of processes in the context communicator. */
    [[nodiscard]] unsigned int n_mpi_processes() const { return dealii::Utilities::MPI::n_mpi_processes(mpi_comm_); }

    /** \brief Borrow the centralized log stream with deal.II's default setup. */
    [[nodiscard]] dealii::LogStream& log_stream() noexcept { return log_stream_; }

    /** \brief Borrow std::cout output enabled only on communicator rank zero. */
    [[nodiscard]] dealii::ConditionalOStream& pcout() noexcept { return pcout_; }

    /**
     * \brief Borrow the wall-time timer with automatic output disabled.
     *
     * Timed sections synchronize over mpi_comm() and require matching calls
     * across ranks. Use TimerOutput::Scope to close sections before teardown.
     * The timer must not be used concurrently by multiple threads.
     */
    [[nodiscard]] dealii::TimerOutput& timer() noexcept { return timer_; }

private:
    /**
     * \brief Initialize MPI before constructing the services that depend on it.
     * \param argc Argument count passed through to MPI initialization.
     * \param argv Argument vector passed through to MPI initialization.
     * \param mpi_comm Borrowed communicator that must outlive the services.
     * Initialization and allocation failures propagate to make_context().
     */
    ContextImpl(int& argc, char**& argv, MPI_Comm mpi_comm) :
        mpi_init_finalize_(argc, argv),
        mpi_comm_(mpi_comm),
        pcout_(std::cout, this_mpi_process() == 0),
        timer_(mpi_comm_, pcout_, dealii::TimerOutput::never, dealii::TimerOutput::wall_times)
    {
    }

    /** \brief Initialize MPI first and finalize it after all other services are destroyed. */
    dealii::Utilities::MPI::MPI_InitFinalize mpi_init_finalize_;
    /** \brief Borrowed communicator used for rank queries and collective timing. */
    MPI_Comm mpi_comm_;

    /** \brief Centralized log stream using deal.II's default configuration. */
    dealii::LogStream log_stream_;
    /** \brief std::cout wrapper enabled only on communicator rank zero. */
    dealii::ConditionalOStream pcout_;
    /** \brief Collective wall-time measurements with automatic output disabled. */
    dealii::TimerOutput timer_;

    /** \brief Next local phase index, protected by mutex_. */
    std::size_t n_phases_registered_{0};
    /** \brief Serialize local phase-index allocation. */
    std::mutex mutex_;

    /** \brief Allow only the factory to construct the shared services. */
    friend Context rift::make_context(int& argc, char**& argv, MPI_Comm mpi_comm);
};

inline ContextImpl& context_impl(const Context& context) noexcept { return *context.impl_; }

} // namespace detail

inline Context make_context(int& argc, char**& argv, MPI_Comm mpi_comm)
{
    // make_shared cannot access the private implementation constructor.
    return Context(std::shared_ptr<detail::ContextImpl>(new detail::ContextImpl(argc, argv, mpi_comm)));
}

} // namespace rift
