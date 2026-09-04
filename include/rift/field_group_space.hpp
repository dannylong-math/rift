#pragma once

/**
 * \file
 * \brief Immutable finite-element spaces for canonical field groups.
 */

#include <cstdint>
#include <deal.II/base/index_set.h>
#include <deal.II/base/types.h>
#include <deal.II/dofs/dof_handler.h>
#include <deal.II/fe/fe_system.h>
#include <deal.II/grid/tria.h>
#include <deal.II/hp/fe_collection.h>
#include <deal.II/lac/affine_constraints.h>
#include <expected>
#include <memory>
#include <rift/phase_support.hpp>
#include <rift/space_draft.hpp>
#include <string>
#include <vector>

namespace rift {

/** \brief Select the DoF numbering applied while field spaces are built. */
enum class DofNumbering : std::uint8_t {
    /** \brief Preserve deal.II's native numbering. */
    native,
    /** \brief Group DoFs by finite-element component. */
    component_wise,
};

/** \brief Configure collective construction of every field-group space. */
struct FieldSpaceBuildOptions {
    /** \brief Numbering policy applied uniformly to every field group. */
    DofNumbering numbering = DofNumbering::native;
};

/** \brief Classify a recoverable field-space preflight defect. */
enum class FieldSpaceBuildErrorCode : std::uint8_t {
    /** \brief One rank supplied a draft that is no longer active. */
    inactive_draft,
    /** \brief One rank supplied a draft whose field spaces already exist. */
    field_spaces_already_built,
    /** \brief Ranks supplied drafts from different space epochs. */
    collective_space_epoch_mismatch,
    /** \brief Ranks supplied drafts with different active or built states. */
    collective_draft_state_mismatch,
    /** \brief Ranks requested different DoF-numbering policies. */
    collective_dof_numbering_mismatch,
};

/** \brief Describe one rank-local field-space preflight error. */
struct FieldSpaceBuildError {
    /** \brief Machine-readable rule that was violated. */
    FieldSpaceBuildErrorCode code;
    /** \brief World rank whose draft or option caused the error. */
    unsigned int rank;
    /** \brief Human-readable description of the conflicting value. */
    std::string message;
};

/** \brief Complete deterministic set of collective field-space errors. */
using FieldSpaceBuildErrors = std::vector<FieldSpaceBuildError>;

/** \brief Result of collective field-space construction. */
using FieldSpaceBuildResult = std::expected<void, FieldSpaceBuildErrors>;

/**
 * \brief Own one support-restricted field group's finite-element space.
 *
 * The ordinary finite element is active on the group's closed phase support;
 * the non-dominating `FE_Nothing` is active everywhere else. Move construction
 * transfers its stable implementation allocation. A moved-from object must
 * not be queried, and assignment is disabled to preserve object identity.
 *
 * \tparam dim volume-mesh dimension; only 2 and 3 are supported.
 */
template<int dim>
    requires(dim == 2 || dim == 3)
class PhaseSupportFieldGroupSpace {
public:
    /** \brief Fixed index of the ordinary component-compatible `FESystem`. */
    static constexpr dealii::types::fe_index ordinary_fe_index = 0;
    /** \brief Fixed index of the non-dominating `FE_Nothing`. */
    static constexpr dealii::types::fe_index outside_support_fe_index = 1;

    /** \brief Destroy the stable finite-element implementation allocation. */
    ~PhaseSupportFieldGroupSpace();
    /** \brief Copy construction is disabled for the sole owner. */
    PhaseSupportFieldGroupSpace(const PhaseSupportFieldGroupSpace&) = delete;
    /** \brief Copy assignment is disabled for the sole owner. */
    PhaseSupportFieldGroupSpace& operator=(const PhaseSupportFieldGroupSpace&) = delete;
    /** \brief Move construction transfers the sole implementation owner. */
    PhaseSupportFieldGroupSpace(PhaseSupportFieldGroupSpace&&) noexcept;
    /** \brief Move assignment is disabled to preserve immutable identity. */
    PhaseSupportFieldGroupSpace& operator=(PhaseSupportFieldGroupSpace&&) = delete;

    /** \brief Return the copied canonical descriptor for this field group. */
    [[nodiscard]] const PhaseSupportFieldGroupDescriptor& descriptor() const noexcept;
    /** \brief Return the draft epoch under which this space was built. */
    [[nodiscard]] SpaceEpoch epoch() const noexcept;
    /** \brief Return the DoF-numbering policy used during construction. */
    [[nodiscard]] DofNumbering numbering() const noexcept;
    /** \brief Return the support aggregate used to select active cells. */
    [[nodiscard]] PhaseSupportSetId phase_support_set_id() const noexcept;
    /** \brief Return the canonical phase whose support selects active cells. */
    [[nodiscard]] PhaseId phase() const noexcept;

    /** \brief Borrow the fixed ordinary/outside-support FE collection. */
    [[nodiscard]] const dealii::hp::FECollection<dim>& finite_elements() const noexcept;
    /** \brief Borrow the distributed DoF handler for this field group. */
    [[nodiscard]] const dealii::DoFHandler<dim>& dof_handler() const noexcept;
    /** \brief Borrow the closed hanging-node constraints. */
    [[nodiscard]] const dealii::AffineConstraints<double>& constraints() const noexcept;
    /** \brief Borrow the handler's authoritative locally owned DoF set. */
    [[nodiscard]] const dealii::IndexSet& locally_owned_dofs() const noexcept;
    /** \brief Borrow the retained locally relevant DoF set. */
    [[nodiscard]] const dealii::IndexSet& locally_relevant_dofs() const noexcept;

private:
    friend class RiftContext;

    /** \brief Build one support-restricted space after collective preflight. */
    PhaseSupportFieldGroupSpace(PhaseSupportFieldGroupDescriptor descriptor, SpaceEpoch epoch, DofNumbering numbering,
                                PhaseSupportSetId phase_support_set_id, const PhaseSupport& support,
                                const dealii::Triangulation<dim>& triangulation);

    /** \brief Opaque stable owner of deal.II objects and provenance. */
    struct Impl;
    /** \brief Stable implementation allocation transferred by public moves. */
    std::unique_ptr<Impl> implementation_;
};

/**
 * \brief Own one background-mesh geometry field group's finite-element space.
 *
 * Its plain `FESystem` is active on the complete mesh. Move construction
 * transfers its stable implementation allocation. A moved-from object must
 * not be queried, and assignment is disabled to preserve object identity.
 *
 * \tparam dim volume-mesh dimension; only 2 and 3 are supported.
 */
template<int dim>
    requires(dim == 2 || dim == 3)
class GeometryFieldGroupSpace {
public:
    /** \brief Destroy the stable finite-element implementation allocation. */
    ~GeometryFieldGroupSpace();
    /** \brief Copy construction is disabled for the sole owner. */
    GeometryFieldGroupSpace(const GeometryFieldGroupSpace&) = delete;
    /** \brief Copy assignment is disabled for the sole owner. */
    GeometryFieldGroupSpace& operator=(const GeometryFieldGroupSpace&) = delete;
    /** \brief Move construction transfers the sole implementation owner. */
    GeometryFieldGroupSpace(GeometryFieldGroupSpace&&) noexcept;
    /** \brief Move assignment is disabled to preserve immutable identity. */
    GeometryFieldGroupSpace& operator=(GeometryFieldGroupSpace&&) = delete;

    /** \brief Return the copied canonical descriptor for this field group. */
    [[nodiscard]] const GeometryFieldGroupDescriptor& descriptor() const noexcept;
    /** \brief Return the draft epoch under which this space was built. */
    [[nodiscard]] SpaceEpoch epoch() const noexcept;
    /** \brief Return the DoF-numbering policy used during construction. */
    [[nodiscard]] DofNumbering numbering() const noexcept;

    /** \brief Borrow the plain background-mesh finite-element system. */
    [[nodiscard]] const dealii::FESystem<dim>& finite_element() const noexcept;
    /** \brief Borrow the distributed DoF handler for this field group. */
    [[nodiscard]] const dealii::DoFHandler<dim>& dof_handler() const noexcept;
    /** \brief Borrow the closed hanging-node constraints. */
    [[nodiscard]] const dealii::AffineConstraints<double>& constraints() const noexcept;
    /** \brief Borrow the handler's authoritative locally owned DoF set. */
    [[nodiscard]] const dealii::IndexSet& locally_owned_dofs() const noexcept;
    /** \brief Borrow the retained locally relevant DoF set. */
    [[nodiscard]] const dealii::IndexSet& locally_relevant_dofs() const noexcept;

private:
    friend class RiftContext;

    /** \brief Build one background-mesh space after collective preflight. */
    GeometryFieldGroupSpace(GeometryFieldGroupDescriptor descriptor, SpaceEpoch epoch, DofNumbering numbering,
                            const dealii::Triangulation<dim>& triangulation);

    /** \brief Opaque stable owner of deal.II objects and provenance. */
    struct Impl;
    /** \brief Stable implementation allocation transferred by public moves. */
    std::unique_ptr<Impl> implementation_;
};

} // namespace rift
