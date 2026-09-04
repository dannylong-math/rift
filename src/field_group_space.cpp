/**
 * \file
 * \brief Stable implementations for immutable field-group spaces.
 */

#include <algorithm>
#include <deal.II/base/index_set.h>
#include <deal.II/dofs/dof_accessor.h>
#include <deal.II/dofs/dof_handler.h>
#include <deal.II/dofs/dof_renumbering.h>
#include <deal.II/dofs/dof_tools.h>
#include <deal.II/fe/fe_nothing.h>
#include <deal.II/fe/fe_q.h>
#include <deal.II/fe/fe_system.h>
#include <deal.II/grid/cell_id.h>
#include <deal.II/grid/tria.h>
#include <deal.II/hp/fe_collection.h>
#include <deal.II/lac/affine_constraints.h>
#include <memory>
#include <rift/field_group_space.hpp>
#include <rift/phase_graph.hpp>
#include <rift/phase_support.hpp>
#include <rift/space_draft.hpp>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>

namespace rift {

namespace {

/** \brief Compare valid phase-support cell identities in their stored order. */
struct CellIdLess {
    /** \brief Apply deal.II's total ordering for valid cell identities. */
    [[nodiscard]] bool operator()(const dealii::CellId& left, const dealii::CellId& right) const
    {
        return left < right;
    }
};

/** \brief Test membership in one sorted partition of a phase support. */
[[nodiscard]] bool contains_cell(const std::span<const dealii::CellId> cells, const dealii::CellId& cell)
{
    return std::ranges::binary_search(cells, cell, CellIdLess{});
}

/** \brief Build the fixed ordinary/outside-support finite-element collection. */
template<int dim>
[[nodiscard]] dealii::hp::FECollection<dim>
make_phase_support_finite_elements(const PhaseSupportFieldGroupDescriptor& descriptor)
{
    const dealii::FESystem<dim> ordinary(dealii::FE_Q<dim>(descriptor.degree), descriptor.component_count);
    // Matching FESystem structures let deal.II compare the component-wise
    // domination rules during hp DoF distribution.
    const dealii::FESystem<dim> outside_support(dealii::FE_Nothing<dim>(1, false), descriptor.component_count);
    return dealii::hp::FECollection<dim>(ordinary, outside_support);
}

/** \brief Select ordinary or outside-support elements on locally owned cells. */
template<int dim>
void select_phase_support_finite_elements(dealii::DoFHandler<dim>& dof_handler, const PhaseSupport& support)
{
    for (const auto& cell : dof_handler.active_cell_iterators()) {
        if (!cell->is_locally_owned()) {
            continue;
        }

        const auto cell_id = cell->id();
        const bool is_supported =
            contains_cell(support.requested_cells(), cell_id) || contains_cell(support.closure_added_cells(), cell_id);
        cell->set_active_fe_index(is_supported ? PhaseSupportFieldGroupSpace<dim>::ordinary_fe_index
                                               : PhaseSupportFieldGroupSpace<dim>::outside_support_fe_index);
    }
}

/** \brief Apply the selected numbering before dependent objects are created. */
template<int dim> void apply_numbering(dealii::DoFHandler<dim>& dof_handler, const DofNumbering numbering)
{
    if (numbering == DofNumbering::component_wise) {
        dealii::DoFRenumbering::component_wise(dof_handler);
    }
}

/** \brief Retain relevant indices and close hanging-node constraints. */
template<int dim>
void build_constraints(const dealii::DoFHandler<dim>& dof_handler, dealii::IndexSet& locally_relevant_dofs,
                       dealii::AffineConstraints<double>& constraints)
{
    locally_relevant_dofs = dealii::DoFTools::extract_locally_relevant_dofs(dof_handler);
    constraints.reinit(dof_handler.locally_owned_dofs(), locally_relevant_dofs);
    if (dof_handler.n_dofs() != 0) {
        dealii::DoFTools::make_hanging_node_constraints(dof_handler, constraints);
    }
    constraints.close();
}

} // namespace

/** \brief Stable implementation storage for one support-restricted field group. */
template<int dim>
    requires(dim == 2 || dim == 3)
struct PhaseSupportFieldGroupSpace<dim>::Impl {
    /** \brief Construct every deal.II object in dependency order. */
    Impl(PhaseSupportFieldGroupDescriptor field_descriptor, const SpaceEpoch space_epoch,
         const DofNumbering dof_numbering, const PhaseSupportSetId support_set_id, const PhaseSupport& support,
         const dealii::Triangulation<dim>& triangulation) :
        descriptor(std::move(field_descriptor)),
        epoch(space_epoch),
        numbering(dof_numbering),
        phase_support_set_id(support_set_id),
        finite_elements(make_phase_support_finite_elements<dim>(descriptor)),
        dof_handler(triangulation)
    {
        select_phase_support_finite_elements(dof_handler, support);
        dof_handler.distribute_dofs(finite_elements);
        apply_numbering(dof_handler, numbering);
        build_constraints(dof_handler, locally_relevant_dofs, constraints);
    }

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
    /** \brief Retained locally relevant DoF set. */
    dealii::IndexSet locally_relevant_dofs;
    /** \brief Closed hanging-node constraints for this field group. */
    dealii::AffineConstraints<double> constraints;
};

template<int dim>
    requires(dim == 2 || dim == 3)
PhaseSupportFieldGroupSpace<dim>::PhaseSupportFieldGroupSpace(PhaseSupportFieldGroupDescriptor descriptor,
                                                              const SpaceEpoch epoch, const DofNumbering numbering,
                                                              const PhaseSupportSetId phase_support_set_id,
                                                              const PhaseSupport& support,
                                                              const dealii::Triangulation<dim>& triangulation) :
    implementation_(
        std::make_unique<Impl>(std::move(descriptor), epoch, numbering, phase_support_set_id, support, triangulation))
{
}

template<int dim>
    requires(dim == 2 || dim == 3)
PhaseSupportFieldGroupSpace<dim>::~PhaseSupportFieldGroupSpace() = default;

template<int dim>
    requires(dim == 2 || dim == 3)
PhaseSupportFieldGroupSpace<dim>::PhaseSupportFieldGroupSpace(PhaseSupportFieldGroupSpace&&) noexcept = default;

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

/** \brief Stable implementation storage for one background-mesh field group. */
template<int dim>
    requires(dim == 2 || dim == 3)
struct GeometryFieldGroupSpace<dim>::Impl {
    /** \brief Construct every deal.II object in dependency order. */
    Impl(GeometryFieldGroupDescriptor field_descriptor, const SpaceEpoch space_epoch, const DofNumbering dof_numbering,
         const dealii::Triangulation<dim>& triangulation) :
        descriptor(std::move(field_descriptor)),
        epoch(space_epoch),
        numbering(dof_numbering),
        finite_element(dealii::FE_Q<dim>(descriptor.degree), descriptor_component_count(descriptor)),
        dof_handler(triangulation)
    {
        dof_handler.distribute_dofs(finite_element);
        apply_numbering(dof_handler, numbering);
        build_constraints(dof_handler, locally_relevant_dofs, constraints);
    }

    /** \brief Return the canonical component count for either geometry binding. */
    [[nodiscard]] static unsigned int descriptor_component_count(const GeometryFieldGroupDescriptor& descriptor)
    {
        return std::visit(
            [](const auto& components) -> unsigned int {
                using Components = std::remove_cvref_t<decltype(components)>;
                if constexpr (std::is_same_v<Components, UnboundFieldComponents>) {
                    return components.count;
                }
                else {
                    return static_cast<unsigned int>(components.phases.size());
                }
            },
            descriptor.components);
    }

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
    /** \brief Retained locally relevant DoF set. */
    dealii::IndexSet locally_relevant_dofs;
    /** \brief Closed hanging-node constraints for this field group. */
    dealii::AffineConstraints<double> constraints;
};

template<int dim>
    requires(dim == 2 || dim == 3)
GeometryFieldGroupSpace<dim>::GeometryFieldGroupSpace(GeometryFieldGroupDescriptor descriptor, const SpaceEpoch epoch,
                                                      const DofNumbering numbering,
                                                      const dealii::Triangulation<dim>& triangulation) :
    implementation_(std::make_unique<Impl>(std::move(descriptor), epoch, numbering, triangulation))
{
}

template<int dim>
    requires(dim == 2 || dim == 3)
GeometryFieldGroupSpace<dim>::~GeometryFieldGroupSpace() = default;

template<int dim>
    requires(dim == 2 || dim == 3)
GeometryFieldGroupSpace<dim>::GeometryFieldGroupSpace(GeometryFieldGroupSpace&&) noexcept = default;

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
