/**
 * \file
 * \brief Stable implementations for immutable field-group spaces.
 */

#include <deal.II/base/index_set.h>
#include <deal.II/dofs/dof_handler.h>
#include <deal.II/fe/fe_system.h>
#include <deal.II/hp/fe_collection.h>
#include <deal.II/lac/affine_constraints.h>
#include <rift/field_group_space.hpp>
#include <rift/phase_graph.hpp>
#include <rift/phase_support.hpp>
#include <rift/space_draft.hpp>

namespace rift {

template<int dim>
    requires(dim == 2 || dim == 3)
struct PhaseSupportFieldGroupSpace<dim>::Impl {
    /** \brief Construction is provided with the field-space builder implementation. */
    Impl() = delete;

    /** \brief Small canonical descriptor copied from the owning draft. */
    PhaseSupportFieldGroupDescriptor descriptor;
    /** \brief Draft epoch under which this space was built. */
    SpaceEpoch epoch;
    /** \brief DoF-numbering policy applied during construction. */
    DofNumbering numbering;
    /** \brief Identity of the support aggregate used during construction. */
    PhaseSupportSetId phase_support_set_id;
    /** \brief Ordinary and outside-support elements in their fixed order. */
    dealii::hp::FECollection<dim> finite_elements;
    /** \brief Distributed DoF handler observing the retained mesh and elements. */
    dealii::DoFHandler<dim> dof_handler;
    /** \brief Closed hanging-node constraints for this field group. */
    dealii::AffineConstraints<double> constraints;
    /** \brief Retained locally relevant DoF set. */
    dealii::IndexSet locally_relevant_dofs;
};

template<int dim>
    requires(dim == 2 || dim == 3)
PhaseSupportFieldGroupSpace<dim>::~PhaseSupportFieldGroupSpace() = default;

template<int dim>
    requires(dim == 2 || dim == 3)
PhaseSupportFieldGroupSpace<dim>::PhaseSupportFieldGroupSpace(PhaseSupportFieldGroupSpace&&) noexcept = default;

template<int dim>
    requires(dim == 2 || dim == 3)
PhaseSupportFieldGroupSpace<dim>&
PhaseSupportFieldGroupSpace<dim>::operator=(PhaseSupportFieldGroupSpace&&) noexcept = default;

template<int dim>
    requires(dim == 2 || dim == 3)
const PhaseSupportFieldGroupDescriptor& PhaseSupportFieldGroupSpace<dim>::descriptor() const noexcept
{
    return implementation_->descriptor;
}

template<int dim>
    requires(dim == 2 || dim == 3)
SpaceEpoch PhaseSupportFieldGroupSpace<dim>::epoch() const noexcept
{
    return implementation_->epoch;
}

template<int dim>
    requires(dim == 2 || dim == 3)
DofNumbering PhaseSupportFieldGroupSpace<dim>::numbering() const noexcept
{
    return implementation_->numbering;
}

template<int dim>
    requires(dim == 2 || dim == 3)
PhaseSupportSetId PhaseSupportFieldGroupSpace<dim>::phase_support_set_id() const noexcept
{
    return implementation_->phase_support_set_id;
}

template<int dim>
    requires(dim == 2 || dim == 3)
PhaseId PhaseSupportFieldGroupSpace<dim>::phase() const noexcept
{
    return implementation_->descriptor.phase;
}

template<int dim>
    requires(dim == 2 || dim == 3)
const dealii::hp::FECollection<dim>& PhaseSupportFieldGroupSpace<dim>::finite_elements() const noexcept
{
    return implementation_->finite_elements;
}

template<int dim>
    requires(dim == 2 || dim == 3)
const dealii::DoFHandler<dim>& PhaseSupportFieldGroupSpace<dim>::dof_handler() const noexcept
{
    return implementation_->dof_handler;
}

template<int dim>
    requires(dim == 2 || dim == 3)
const dealii::AffineConstraints<double>& PhaseSupportFieldGroupSpace<dim>::constraints() const noexcept
{
    return implementation_->constraints;
}

template<int dim>
    requires(dim == 2 || dim == 3)
const dealii::IndexSet& PhaseSupportFieldGroupSpace<dim>::locally_owned_dofs() const noexcept
{
    return implementation_->dof_handler.locally_owned_dofs();
}

template<int dim>
    requires(dim == 2 || dim == 3)
const dealii::IndexSet& PhaseSupportFieldGroupSpace<dim>::locally_relevant_dofs() const noexcept
{
    return implementation_->locally_relevant_dofs;
}

template<int dim>
    requires(dim == 2 || dim == 3)
struct GeometryFieldGroupSpace<dim>::Impl {
    /** \brief Construction is provided with the field-space builder implementation. */
    Impl() = delete;

    /** \brief Small canonical descriptor copied from the owning draft. */
    GeometryFieldGroupDescriptor descriptor;
    /** \brief Draft epoch under which this space was built. */
    SpaceEpoch epoch;
    /** \brief DoF-numbering policy applied during construction. */
    DofNumbering numbering;
    /** \brief Plain finite-element system active on the background mesh. */
    dealii::FESystem<dim> finite_element;
    /** \brief Distributed DoF handler observing the retained mesh and element. */
    dealii::DoFHandler<dim> dof_handler;
    /** \brief Closed hanging-node constraints for this field group. */
    dealii::AffineConstraints<double> constraints;
    /** \brief Retained locally relevant DoF set. */
    dealii::IndexSet locally_relevant_dofs;
};

template<int dim>
    requires(dim == 2 || dim == 3)
GeometryFieldGroupSpace<dim>::~GeometryFieldGroupSpace() = default;

template<int dim>
    requires(dim == 2 || dim == 3)
GeometryFieldGroupSpace<dim>::GeometryFieldGroupSpace(GeometryFieldGroupSpace&&) noexcept = default;

template<int dim>
    requires(dim == 2 || dim == 3)
GeometryFieldGroupSpace<dim>& GeometryFieldGroupSpace<dim>::operator=(GeometryFieldGroupSpace&&) noexcept = default;

template<int dim>
    requires(dim == 2 || dim == 3)
const GeometryFieldGroupDescriptor& GeometryFieldGroupSpace<dim>::descriptor() const noexcept
{
    return implementation_->descriptor;
}

template<int dim>
    requires(dim == 2 || dim == 3)
SpaceEpoch GeometryFieldGroupSpace<dim>::epoch() const noexcept
{
    return implementation_->epoch;
}

template<int dim>
    requires(dim == 2 || dim == 3)
DofNumbering GeometryFieldGroupSpace<dim>::numbering() const noexcept
{
    return implementation_->numbering;
}

template<int dim>
    requires(dim == 2 || dim == 3)
const dealii::FESystem<dim>& GeometryFieldGroupSpace<dim>::finite_element() const noexcept
{
    return implementation_->finite_element;
}

template<int dim>
    requires(dim == 2 || dim == 3)
const dealii::DoFHandler<dim>& GeometryFieldGroupSpace<dim>::dof_handler() const noexcept
{
    return implementation_->dof_handler;
}

template<int dim>
    requires(dim == 2 || dim == 3)
const dealii::AffineConstraints<double>& GeometryFieldGroupSpace<dim>::constraints() const noexcept
{
    return implementation_->constraints;
}

template<int dim>
    requires(dim == 2 || dim == 3)
const dealii::IndexSet& GeometryFieldGroupSpace<dim>::locally_owned_dofs() const noexcept
{
    return implementation_->dof_handler.locally_owned_dofs();
}

template<int dim>
    requires(dim == 2 || dim == 3)
const dealii::IndexSet& GeometryFieldGroupSpace<dim>::locally_relevant_dofs() const noexcept
{
    return implementation_->locally_relevant_dofs;
}

template class PhaseSupportFieldGroupSpace<2>;
template class PhaseSupportFieldGroupSpace<3>;
template class GeometryFieldGroupSpace<2>;
template class GeometryFieldGroupSpace<3>;

} // namespace rift
