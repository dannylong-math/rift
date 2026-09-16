#pragma once

/**
 * \file
 * \brief Ownership and basic operations for a distributed discretization.
 */

#include "rift/context.hpp"

#include <deal.II/base/enable_observer_pointer.h>
#include <deal.II/base/observer_pointer.h>
#include <deal.II/base/types.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <string>

namespace rift {

/**
 * \brief Own the distributed background mesh for one Rift simulation.
 * \tparam dim Spatial dimension, either 2 or 3 for the distributed backend.
 *
 * Discretization creates, refines, queries, and exposes the
 * dealii::parallel::distributed::Triangulation on which phase discretizations
 * will be built. Degree-of-freedom handlers and finite-element systems are not
 * yet owned by this class.
 *
 * The supplied Context must outlive the Discretization. The object is
 * noncopyable and nonmovable so other Rift components may safely observe its
 * stable address. Mesh mutation can invalidate iterators and data owned by
 * those components; callers remain responsible for coordinating such updates.
 *
 * \par Typical use
 * \code{.cpp}
 * int main(int argc, char** argv) {
 *   rift::Context context(argc, argv);
 *   rift::Discretization<2> discretization(context);
 *
 *   discretization.generate_grid("hyper_cube", "0 : 1 : false");
 *   discretization.refine_global(2);
 *   context.pcout() << discretization.n_global_active_cells()
 *                   << " active cells\n";
 * }
 * \endcode
 */
template<int dim>
    requires(dim == 2 || dim == 3)
class Discretization : public dealii::EnableObserverPointer {
public:
    /**
     * \brief Construct an empty distributed mesh using a Context communicator.
     * \param context Borrowed mutable context, which must outlive this object.
     */
    Discretization(Context& context) :
        context_(&context, "Discretization"),
        triangulation_(context_->mpi_comm(), typename dealii::Triangulation<dim>::MeshSmoothing(
                                                 dealii::Triangulation<dim>::smoothing_on_refinement |
                                                 dealii::Triangulation<dim>::smoothing_on_coarsening))
    {
    }

    /** \brief A discretization has one owner and cannot be copied. */
    Discretization(const Discretization&) = delete;
    /** \brief Ownership cannot be replaced by copying. */
    Discretization& operator=(const Discretization&) = delete;
    /** \brief Preserve a stable address for observers and borrowed objects. */
    Discretization(Discretization&&) = delete;
    /** \brief Ownership cannot be replaced by moving. */
    Discretization& operator=(Discretization&&) = delete;
    /** \brief Destroy the owned triangulation. */
    ~Discretization() = default;

    /**
     * \brief Generate a coarse mesh using deal.II's named grid generators.
     * \param grid_generator_function_name Supported GridGenerator function name.
     * \param grid_generator_function_args Tuple-formatted arguments, including
     * optional arguments; for example, "0 : 1 : false" for "hyper_cube".
     * \pre The triangulation is empty and the chosen generator produces a mesh
     * supported by the distributed backend (quadrilaterals or hexahedra).
     * \pre All communicator ranks call with the same name and arguments.
     * \throws dealii::ExceptionBase If name selection or argument parsing fails.
     *
     * Generator failures also propagate. No general rollback guarantee is
     * added to deal.II's behavior. To replace a mesh, first clear it through
     * triangulation(), obeying the lifetime rules of any attached objects.
     */
    void generate_grid(const std::string& grid_generator_function_name, const std::string& grid_generator_function_args)
    {
        dealii::GridGenerator::generate_from_name_and_arguments(triangulation_, grid_generator_function_name,
                                                                grid_generator_function_args);
    }

    /**
     * \brief Return the global number of active cells across the communicator.
     * \return Sum of uniquely owned active cells, or zero for an empty mesh.
     * Ghost and artificial cells are not counted again.
     */
    [[nodiscard]] dealii::types::global_cell_index n_global_active_cells() const noexcept
    {
        return triangulation_.n_global_active_cells();
    }
    /**
     * \brief Return the active-cell count stored on this rank.
     * \return Number of owned, ghost, and artificial active cells, or zero for
     * an empty mesh. Summing this count over ranks double-counts shared cells.
     */
    [[nodiscard]] unsigned int n_active_cells() const noexcept { return triangulation_.n_active_cells(); }

    /**
     * \brief Return the number of refinement levels present on this rank.
     * \return Highest locally stored active-cell level plus one, or zero for an
     * empty mesh. This can differ from the maximum level count across ranks.
     */
    [[nodiscard]] unsigned int n_levels() const noexcept { return triangulation_.n_levels(); }

    /**
     * \brief Perform the requested number of global mesh refinements.
     * \param n_global_refinements Number of successive isotropic refinements;
     * zero leaves a valid nonempty mesh unchanged.
     * \pre The triangulation is nonempty, even for zero refinements.
     * \pre All communicator ranks call with the same refinement count.
     *
     * Mesh changes can invalidate cell iterators and dependent data. The caller
     * is responsible for field transfer and cache updates.
     */
    void refine_global(unsigned int n_global_refinements = 1) { triangulation_.refine_global(n_global_refinements); }

    /**
     * \brief Borrow read-only access to the owned triangulation.
     * \return Reference valid only while this Discretization is alive.
     */
    [[nodiscard]] const dealii::parallel::distributed::Triangulation<dim>& triangulation() const noexcept
    {
        return triangulation_;
    }
    /**
     * \brief Borrow mutable access to the owned triangulation.
     * \return Reference valid only while this Discretization is alive.
     * Obtaining the reference does not throw.
     *
     * Direct modifications are reflected by this object's queries. The caller
     * must obey deal.II's collective requirements and handle invalidation of
     * dependent data; this accessor does not coordinate those updates.
     */
    [[nodiscard]] dealii::parallel::distributed::Triangulation<dim>& triangulation() noexcept { return triangulation_; }

private:
    /** \brief Borrowed simulation context that supplies the communicator. */
    dealii::ObserverPointer<Context> context_;
    /** \brief Distributed mesh owned by this discretization. */
    dealii::parallel::distributed::Triangulation<dim> triangulation_;
};
} // namespace rift
