#pragma once

/**
 * \file
 * \brief Representation-neutral field schema and move-only space draft.
 */

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <rift/phase_graph.hpp>
#include <rift/phase_support.hpp>
#include <rift/strong_id.hpp>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace rift {

class RiftContext;

template<int dim>
    requires(dim == 2 || dim == 3)
class PhaseSupportFieldGroupSpace;

template<int dim>
    requires(dim == 2 || dim == 3)
class GeometryFieldGroupSpace;

/** \brief Implementation tags for field-schema identities. */
namespace detail {

/** \brief Distinguish phase-support field-group identifiers. */
struct PhaseSupportFieldGroupIdTag {};

/** \brief Distinguish geometry field-group identifiers. */
struct GeometryFieldGroupIdTag {};

/** \brief Distinguish discrete geometry-metadata identifiers. */
struct DiscreteGeometryMetadataIdTag {};

/** \brief Distinguish provisional space epochs from other identities. */
struct SpaceEpochTag {};

/** \brief Hide field-space owning containers from the schema header. */
template<int dim>
    requires(dim == 2 || dim == 3)
struct FieldSpaceStorage;

} // namespace detail

/** \brief Identify one canonical phase-support field group. */
using PhaseSupportFieldGroupId = StrongId<detail::PhaseSupportFieldGroupIdTag>;

/** \brief Identify one canonical continuous geometry field group. */
using GeometryFieldGroupId = StrongId<detail::GeometryFieldGroupIdTag>;

/** \brief Identify one canonical discrete geometry-metadata block. */
using DiscreteGeometryMetadataId = StrongId<detail::DiscreteGeometryMetadataIdTag>;

/** \brief Identify one context-local provisional finite-element space. */
using SpaceEpoch = StrongId<detail::SpaceEpochTag, std::uint64_t>;

/** \brief Request one finite-element field group restricted to a phase support. */
struct PhaseSupportFieldGroupSpecification {
    /** \brief Canonical phase whose closed support selects active cells. */
    PhaseId phase;
    /** \brief Name unique among support fields for the same phase. */
    std::string name;
    /** \brief Positive number of continuous components. */
    unsigned int component_count;
    /** \brief Positive polynomial degree of the future `FE_Q` element. */
    unsigned int degree;
};

/** \brief Describe continuous geometry components without phase bindings. */
struct UnboundFieldComponents {
    /** \brief Positive number of continuous components. */
    unsigned int count;
};

/** \brief Bind continuous geometry components one-to-one to canonical phases. */
struct PhaseBoundFieldComponents {
    /** \brief Every canonical phase exactly once, canonicalized by `PhaseId`. */
    std::vector<PhaseId> phases;
};

/** \brief Select unbound or completely phase-bound component semantics. */
using GeometryFieldComponents = std::variant<UnboundFieldComponents, PhaseBoundFieldComponents>;

/** \brief Request one background-mesh continuous geometry field group. */
struct GeometryFieldGroupSpecification {
    /** \brief Name unique among continuous geometry field groups. */
    std::string name;
    /** \brief Component count and optional complete canonical phase binding. */
    GeometryFieldComponents components;
    /** \brief Positive polynomial degree of the future `FE_Q` element. */
    unsigned int degree;
};

/** \brief Classify supported logical discrete geometry metadata. */
enum class DiscreteGeometryMetadataKind : std::uint8_t {
    /** \brief Optional canonical phase identity at each associated scalar DoF. */
    phase_label,
};

/** \brief Request discrete metadata aligned with one geometry component. */
struct DiscreteGeometryMetadataSpecification {
    /** \brief Name unique among discrete geometry metadata. */
    std::string name;
    /** \brief Name of the referenced continuous geometry field group. */
    std::string geometry_field_group;
    /** \brief Zero-based component within the referenced group. */
    unsigned int component;
    /** \brief Logical metadata value kind. */
    DiscreteGeometryMetadataKind kind;
};

/** \brief Own primitive continuous geometry fields and optional metadata. */
struct GeometryRepresentationSpecification {
    /** \brief Continuous background-mesh fields in arbitrary input order. */
    std::vector<GeometryFieldGroupSpecification> continuous_fields;
    /** \brief Discrete component-aligned metadata in arbitrary input order. */
    std::vector<DiscreteGeometryMetadataSpecification> discrete_metadata;
};

/** \brief Own the complete unresolved field-schema request. */
struct SpaceSpecification {
    /** \brief Support-restricted field groups in arbitrary input order. */
    std::vector<PhaseSupportFieldGroupSpecification> phase_support_fields;
    /** \brief Primitive representation-neutral geometry schema. */
    GeometryRepresentationSpecification geometry;
};

/** \brief Describe one canonical support-restricted continuous field group. */
struct PhaseSupportFieldGroupDescriptor {
    /** \brief Category-local canonical field-group identity. */
    PhaseSupportFieldGroupId id;
    /** \brief Canonical phase whose support restricts this field group. */
    PhaseId phase;
    /** \brief Configured field-group name. */
    std::string name;
    /** \brief Number of continuous components. */
    unsigned int component_count;
    /** \brief Polynomial degree of the future `FE_Q` element. */
    unsigned int degree;
};

/** \brief Describe one canonical background-mesh continuous geometry field. */
struct GeometryFieldGroupDescriptor {
    /** \brief Category-local canonical field-group identity. */
    GeometryFieldGroupId id;
    /** \brief Configured field-group name. */
    std::string name;
    /** \brief Canonical unbound or phase-bound component semantics. */
    GeometryFieldComponents components;
    /** \brief Polynomial degree of the future `FE_Q` element. */
    unsigned int degree;
};

/** \brief Describe canonical metadata resolved to one geometry field component. */
struct DiscreteGeometryMetadataDescriptor {
    /** \brief Category-local canonical metadata identity. */
    DiscreteGeometryMetadataId id;
    /** \brief Configured metadata name. */
    std::string name;
    /** \brief Resolved continuous geometry field-group identity. */
    GeometryFieldGroupId geometry_field_group;
    /** \brief Zero-based component within the resolved field group. */
    unsigned int component;
    /** \brief Logical metadata value kind. */
    DiscreteGeometryMetadataKind kind;
};

/** \brief Publish the canonical representation-neutral field schema. */
class SpaceSchema {
public:
    /** \brief Return support fields in `(PhaseId, name)` order. */
    [[nodiscard]] std::span<const PhaseSupportFieldGroupDescriptor> phase_support_fields() const noexcept;

    /** \brief Return continuous geometry fields in name order. */
    [[nodiscard]] std::span<const GeometryFieldGroupDescriptor> geometry_fields() const noexcept;

    /** \brief Return discrete geometry metadata in name order. */
    [[nodiscard]] std::span<const DiscreteGeometryMetadataDescriptor> discrete_geometry_metadata() const noexcept;

private:
    friend class RiftContext;

    /** \brief Adopt validated canonical descriptors. */
    SpaceSchema(std::vector<PhaseSupportFieldGroupDescriptor> phase_support_fields,
                std::vector<GeometryFieldGroupDescriptor> geometry_fields,
                std::vector<DiscreteGeometryMetadataDescriptor> discrete_geometry_metadata) noexcept;

    /** \brief Canonical support-field descriptors. */
    std::vector<PhaseSupportFieldGroupDescriptor> phase_support_fields_;
    /** \brief Canonical continuous geometry-field descriptors. */
    std::vector<GeometryFieldGroupDescriptor> geometry_fields_;
    /** \brief Canonical resolved metadata descriptors. */
    std::vector<DiscreteGeometryMetadataDescriptor> discrete_geometry_metadata_;
};

/** \brief Identify one support-field specification in a diagnostic. */
struct PhaseSupportFieldGroupErrorSubject {
    /** \brief Position after canonical input sorting. */
    std::size_t sorted_index;
    /** \brief Complete offending configured value. */
    PhaseSupportFieldGroupSpecification specification;
};

/** \brief Identify one geometry-field specification and optional subcomponent. */
struct GeometryFieldGroupErrorSubject {
    /** \brief Position after canonical input sorting. */
    std::size_t sorted_index;
    /** \brief Complete offending configured value. */
    GeometryFieldGroupSpecification specification;
    /** \brief Offending phase binding when one exists. */
    std::optional<PhaseId> phase;
    /** \brief Offending component when one exists. */
    std::optional<unsigned int> component;
};

/** \brief Identify one discrete geometry-metadata specification. */
struct DiscreteGeometryMetadataErrorSubject {
    /** \brief Position after canonical input sorting. */
    std::size_t sorted_index;
    /** \brief Complete offending configured value. */
    DiscreteGeometryMetadataSpecification specification;
};

/** \brief Typed offending input retained by one space-draft diagnostic. */
using SpaceDraftErrorSubject = std::variant<std::monostate, PhaseSupportFieldGroupErrorSubject,
                                            GeometryFieldGroupErrorSubject, DiscreteGeometryMetadataErrorSubject>;

/** \brief Classify one recoverable space-draft construction defect. */
enum class SpaceDraftErrorCode : std::uint8_t {
    /** \brief The supplied support aggregate no longer owns usable data. */
    inactive_phase_support_set,
    /** \brief One rank invoked the factory with a different dimension. */
    dimension_mismatch,
    /** \brief Ranks supplied support aggregates from different successful calls. */
    phase_support_set_mismatch,
    /** \brief Ranks supplied support aggregates for different mesh snapshots. */
    mesh_snapshot_mismatch,
    /** \brief A configured field or metadata name is empty. */
    empty_name,
    /** \brief A configured field or metadata name is not valid UTF-8. */
    invalid_name_encoding,
    /** \brief A name is repeated within its uniqueness scope. */
    duplicate_name,
    /** \brief A support field references no canonical phase. */
    unknown_phase,
    /** \brief A continuous field group has no components. */
    zero_component_count,
    /** \brief A continuous field group requests degree zero. */
    invalid_polynomial_degree,
    /** \brief A phase-bound geometry field omits a canonical phase. */
    missing_phase_binding,
    /** \brief A phase-bound geometry field repeats a canonical phase. */
    duplicate_phase_binding,
    /** \brief A geometry component binds no canonical phase. */
    unknown_phase_binding,
    /** \brief Metadata cannot uniquely resolve its named geometry field. */
    unknown_metadata_field_group,
    /** \brief Metadata selects no component in its resolved field group. */
    metadata_component_out_of_range,
    /** \brief Phase-label metadata targets a phase-bound field group. */
    metadata_requires_unbound_components,
    /** \brief Metadata requests a logical kind not supported in this milestone. */
    unsupported_metadata_kind,
    /** \brief Multiple metadata blocks target the same geometry component. */
    duplicate_metadata_target,
    /** \brief One rank supplied a different canonical field schema. */
    schema_mismatch,
    /** \brief Private epoch counters differ after a collective-contract violation. */
    space_epoch_mismatch,
};

/** \brief Describe one rank-local or collective space-draft error. */
struct SpaceDraftError {
    /** \brief Machine-readable rule that was violated. */
    SpaceDraftErrorCode code;
    /** \brief World rank whose input caused the error. */
    unsigned int rank;
    /** \brief Exact offending specification when one exists. */
    SpaceDraftErrorSubject subject;
    /** \brief Human-readable description containing configured values. */
    std::string message;
};

/** \brief Complete deterministic collective error set. */
using SpaceDraftErrors = std::vector<SpaceDraftError>;

/**
 * \brief Own validated support and canonical schema before DoF publication.
 *
 * Moving a draft transfers its epoch, schema, and support aggregate and makes
 * the source inactive. Later finalization may also make an active draft
 * inactive. A draft is not a published finite-element space.
 *
 * \tparam dim volume-mesh dimension; only 2 and 3 are supported.
 */
template<int dim>
    requires(dim == 2 || dim == 3)
class SpaceDraft {
public:
    /** \brief Destroy this draft and its owned support aggregate. */
    ~SpaceDraft();
    /** \brief Copy construction is disabled because the support aggregate is move-only. */
    SpaceDraft(const SpaceDraft&) = delete;
    /** \brief Copy assignment is disabled because the support aggregate is move-only. */
    SpaceDraft& operator=(const SpaceDraft&) = delete;
    /** \brief Move construction transfers the draft and deactivates the source. */
    SpaceDraft(SpaceDraft&& other) noexcept;
    /** \brief Move assignment transfers the draft and deactivates the source. */
    SpaceDraft& operator=(SpaceDraft&& other) noexcept;

    /** \brief Return the context-local provisional space identity. */
    [[nodiscard]] SpaceEpoch epoch() const noexcept { return epoch_; }

    /** \brief Report whether this object still owns a usable draft. */
    [[nodiscard]] bool active() const noexcept { return active_; }

    /** \brief Return the canonical field schema owned by this draft. */
    [[nodiscard]] const SpaceSchema& canonical_schema() const noexcept { return schema_; }

    /** \brief Borrow the support aggregate retained by an active draft. */
    [[nodiscard]] const PhaseSupportSet<dim>& phase_supports() const noexcept { return phase_supports_; }

    /** \brief Report whether finite-element spaces were committed successfully. */
    [[nodiscard]] bool field_spaces_built() const noexcept { return field_spaces_ != nullptr; }

    /** \brief Return support-restricted spaces in canonical descriptor order. */
    [[nodiscard]] std::span<const PhaseSupportFieldGroupSpace<dim>> phase_support_field_spaces() const noexcept;

    /** \brief Return geometry spaces in canonical descriptor order. */
    [[nodiscard]] std::span<const GeometryFieldGroupSpace<dim>> geometry_field_spaces() const noexcept;

    /**
     * \brief Resolve one support-restricted field-space identity.
     *
     * \param id canonical category-local field-group identity.
     * \return immutable field-group space for `id`.
     * \throws std::logic_error when field spaces have not been built.
     * \throws std::out_of_range when `id` is invalid after construction.
     */
    [[nodiscard]] const PhaseSupportFieldGroupSpace<dim>& phase_support_field_space(PhaseSupportFieldGroupId id) const;

    /**
     * \brief Resolve one geometry field-space identity.
     *
     * \param id canonical category-local field-group identity.
     * \return immutable field-group space for `id`.
     * \throws std::logic_error when field spaces have not been built.
     * \throws std::out_of_range when `id` is invalid after construction.
     */
    [[nodiscard]] const GeometryFieldGroupSpace<dim>& geometry_field_space(GeometryFieldGroupId id) const;

private:
    friend class RiftContext;

    /** \brief Adopt a validated support set and canonical schema. */
    SpaceDraft(SpaceEpoch epoch, PhaseSupportSet<dim> phase_supports, SpaceSchema schema) noexcept;

    /** \brief Context-local provisional space identity. */
    SpaceEpoch epoch_;
    /** \brief Complete support aggregate owned by this draft. */
    PhaseSupportSet<dim> phase_supports_;
    /** \brief Canonical representation-neutral field schema. */
    SpaceSchema schema_;
    /** \brief Opaque canonical field-space owners, allocated only on commit. */
    std::unique_ptr<detail::FieldSpaceStorage<dim>> field_spaces_;
    /** \brief Whether this object still represents a usable draft. */
    bool active_ = true;
};

/** \brief Result of collective space-draft construction. */
template<int dim>
    requires(dim == 2 || dim == 3)
using SpaceDraftResult = std::expected<SpaceDraft<dim>, SpaceDraftErrors>;

} // namespace rift
