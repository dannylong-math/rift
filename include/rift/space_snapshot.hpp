#pragma once

/**
 * \file
 * \brief Immutable finalized finite-element space and state layout.
 */

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <rift/field_group_space.hpp>
#include <rift/space_draft.hpp>
#include <rift/strong_id.hpp>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace rift {

class RiftContext;

/** \brief Implementation tags and factories for finalized-space identities. */
namespace detail {

/** \brief Distinguish regional-entry identifiers. */
struct RegionalEntryIdTag {};

/** \brief Enable private snapshot construction through `std::make_shared`. */
template<int dim>
    requires(dim == 2 || dim == 3)
class SpaceSnapshotMakeSharedEnabler;

} // namespace detail

/** \brief Identify one canonical nonspatial regional state entry. */
using RegionalEntryId = StrongId<detail::RegionalEntryIdTag>;

/** \brief Bind a support-field ID to the finalized space that defines it. */
struct PhaseSupportFieldReference {
    /** \brief Finalized-space epoch that interprets `field_group`. */
    SpaceEpoch space_epoch;
    /** \brief Category-local support-field identity. */
    PhaseSupportFieldGroupId field_group;
};

/** \brief Bind a geometry-field ID to the finalized space that defines it. */
struct GeometryFieldReference {
    /** \brief Finalized-space epoch that interprets `field_group`. */
    SpaceEpoch space_epoch;
    /** \brief Category-local geometry-field identity. */
    GeometryFieldGroupId field_group;
};

/** \brief Bind a discrete-metadata ID to the finalized space that defines it. */
struct DiscreteGeometryMetadataReference {
    /** \brief Finalized-space epoch that interprets `metadata`. */
    SpaceEpoch space_epoch;
    /** \brief Category-local metadata identity. */
    DiscreteGeometryMetadataId metadata;
};

/** \brief Bind a regional-entry ID to the finalized space that defines it. */
struct RegionalEntryReference {
    /** \brief Finalized-space epoch that interprets `regional_entry`. */
    SpaceEpoch space_epoch;
    /** \brief Category-local regional-entry identity. */
    RegionalEntryId regional_entry;
};

/** \brief Request one scalar associated with a phase-region constraint. */
struct RegionalEntrySpecification {
    /** \brief Canonical phase associated with this scalar. */
    PhaseId phase;
    /** \brief Nonempty UTF-8 name unique within `phase`. */
    std::string name;
};

/** \brief Place one support-restricted continuous field in semantic state order. */
struct PhaseSupportFieldLayoutEntry {
    /** \brief Canonical field-group identity resolved through the snapshot. */
    PhaseSupportFieldGroupId field_group;
    /** \brief Zero-based semantic offset of this block. */
    std::uint64_t offset;
    /** \brief Global number of field-group DoFs. */
    std::uint64_t cardinality;
};

/** \brief Place one background-mesh continuous field in semantic state order. */
struct GeometryFieldLayoutEntry {
    /** \brief Canonical field-group identity resolved through the snapshot. */
    GeometryFieldGroupId field_group;
    /** \brief Zero-based semantic offset of this block. */
    std::uint64_t offset;
    /** \brief Global number of field-group DoFs. */
    std::uint64_t cardinality;
};

/** \brief Place one component-aligned metadata block in semantic state order. */
struct DiscreteGeometryMetadataLayoutEntry {
    /** \brief Canonical metadata identity resolved through the snapshot. */
    DiscreteGeometryMetadataId metadata;
    /** \brief Zero-based semantic offset of this block. */
    std::uint64_t offset;
    /** \brief Global number of DoFs in the referenced scalar component. */
    std::uint64_t cardinality;
};

/** \brief Describe and place one canonical nonspatial regional scalar. */
struct RegionalEntry {
    /** \brief Canonical category-local identity. */
    RegionalEntryId id;
    /** \brief Canonical phase associated with this scalar. */
    PhaseId phase;
    /** \brief Opaque semantic name unique within `phase`. */
    std::string name;
    /** \brief World rank that will own the scalar's authoritative value. */
    unsigned int owner_rank;
    /** \brief Zero-based semantic offset of this scalar. */
    std::uint64_t offset;
    /** \brief Logical cardinality, always one in Milestone 001. */
    std::uint64_t cardinality;
};

/** \brief Publish deterministic semantic offsets without prescribing storage. */
class StateLayout {
public:
    /** \brief Return support-restricted field blocks in canonical order. */
    [[nodiscard]] std::span<const PhaseSupportFieldLayoutEntry> phase_support_fields() const noexcept;
    /** \brief Return background-mesh field blocks in canonical order. */
    [[nodiscard]] std::span<const GeometryFieldLayoutEntry> geometry_fields() const noexcept;
    /** \brief Return component-aligned metadata blocks in canonical order. */
    [[nodiscard]] std::span<const DiscreteGeometryMetadataLayoutEntry> discrete_geometry_metadata() const noexcept;
    /** \brief Return nonspatial regional scalars in canonical order. */
    [[nodiscard]] std::span<const RegionalEntry> regional_entries() const noexcept;
    /** \brief Return the checked sum of every published block cardinality. */
    [[nodiscard]] std::uint64_t total_cardinality() const noexcept;

    /**
     * \brief Resolve one support-field block.
     * \param id canonical category-local identity.
     * \return immutable layout entry for `id`.
     * \throws std::out_of_range when `id` is invalid.
     */
    [[nodiscard]] const PhaseSupportFieldLayoutEntry& phase_support_field(PhaseSupportFieldGroupId id) const;
    /**
     * \brief Resolve one geometry-field block.
     * \param id canonical category-local identity.
     * \return immutable layout entry for `id`.
     * \throws std::out_of_range when `id` is invalid.
     */
    [[nodiscard]] const GeometryFieldLayoutEntry& geometry_field(GeometryFieldGroupId id) const;
    /**
     * \brief Resolve one discrete-metadata block.
     * \param id canonical category-local identity.
     * \return immutable layout entry for `id`.
     * \throws std::out_of_range when `id` is invalid.
     */
    [[nodiscard]] const DiscreteGeometryMetadataLayoutEntry&
    discrete_geometry_metadata(DiscreteGeometryMetadataId id) const;
    /**
     * \brief Resolve one regional scalar.
     * \param id canonical category-local identity.
     * \return immutable regional entry for `id`.
     * \throws std::out_of_range when `id` is invalid.
     */
    [[nodiscard]] const RegionalEntry& regional_entry(RegionalEntryId id) const;

private:
    friend class RiftContext;

    /** \brief Adopt a complete checked layout after collective agreement. */
    StateLayout(std::vector<PhaseSupportFieldLayoutEntry> phase_support_fields,
                std::vector<GeometryFieldLayoutEntry> geometry_fields,
                std::vector<DiscreteGeometryMetadataLayoutEntry> discrete_geometry_metadata,
                std::vector<RegionalEntry> regional_entries, std::uint64_t total_cardinality) noexcept;

    /** \brief Canonical support-field blocks. */
    std::vector<PhaseSupportFieldLayoutEntry> phase_support_fields_;
    /** \brief Canonical geometry-field blocks. */
    std::vector<GeometryFieldLayoutEntry> geometry_fields_;
    /** \brief Canonical discrete-metadata blocks. */
    std::vector<DiscreteGeometryMetadataLayoutEntry> discrete_geometry_metadata_;
    /** \brief Canonical regional scalars. */
    std::vector<RegionalEntry> regional_entries_;
    /** \brief Checked semantic cardinality across all categories. */
    std::uint64_t total_cardinality_;
};

/** \brief Classify one recoverable finalized-space defect. */
enum class SpaceFinalizationErrorCode : std::uint8_t {
    /** \brief One rank supplied a draft that is no longer active. */
    inactive_draft,
    /** \brief One rank supplied a draft created by another context. */
    foreign_draft,
    /** \brief One rank supplied a draft whose continuous spaces are absent. */
    field_spaces_not_built,
    /** \brief A regional entry has an empty name. */
    empty_name,
    /** \brief A regional entry name is not valid UTF-8. */
    invalid_name_encoding,
    /** \brief A regional entry name is repeated for one phase. */
    duplicate_name,
    /** \brief A regional entry references no canonical phase. */
    unknown_phase,
    /** \brief Ranks supplied drafts from different space epochs. */
    collective_space_epoch_mismatch,
    /** \brief Ranks supplied drafts with different lifecycle states. */
    collective_draft_state_mismatch,
    /** \brief Ranks supplied different canonical regional schemas. */
    regional_schema_mismatch,
    /** \brief A layout offset or identity cannot be represented. */
    layout_overflow,
    /** \brief Ranks computed different complete layouts. */
    layout_mismatch,
};

/** \brief Retain the complete offending regional specification. */
struct RegionalEntryErrorSubject {
    /** \brief Position after canonical `(PhaseId, name)` sorting. */
    std::size_t sorted_index;
    /** \brief Exact supplied regional specification. */
    RegionalEntrySpecification specification;
};

/** \brief Classify a state-layout block for diagnostics. */
enum class LayoutEntryKind : std::uint8_t {
    /** \brief Support-restricted continuous field. */
    phase_support_field,
    /** \brief Background-mesh continuous field. */
    geometry_field,
    /** \brief Component-aligned discrete geometry metadata. */
    discrete_geometry_metadata,
    /** \brief Nonspatial regional scalar. */
    regional_scalar,
};

/** \brief Identify the semantic layout block that failed construction. */
struct LayoutEntryErrorSubject {
    /** \brief Category containing the block. */
    LayoutEntryKind kind;
    /** \brief Position within that category's canonical order. */
    std::size_t canonical_index;
    /** \brief Configured field, metadata, or regional name. */
    std::string name;
    /** \brief Phase when the category has phase-qualified semantics. */
    std::optional<PhaseId> phase;
};

/** \brief Attach no subject, a regional input, or a layout block to an error. */
using SpaceFinalizationErrorSubject = std::variant<std::monostate, RegionalEntryErrorSubject, LayoutEntryErrorSubject>;

/** \brief Describe one rank-local or collective space-finalization error. */
struct SpaceFinalizationError {
    /** \brief Machine-readable rule that was violated. */
    SpaceFinalizationErrorCode code;
    /** \brief World rank whose input or candidate caused the error. */
    unsigned int rank;
    /** \brief Typed offending input or layout block when one exists. */
    SpaceFinalizationErrorSubject subject;
    /** \brief Immediately usable human-readable diagnostic. */
    std::string message;
};

/** \brief Complete deterministic collective finalization error set. */
using SpaceFinalizationErrors = std::vector<SpaceFinalizationError>;

/**
 * \brief Own one immutable published finite-element space and semantic layout.
 *
 * The stable allocation retains the exact phase supports, schema, field
 * spaces, and layout that agreed collectively. Callers share only a const
 * handle; the snapshot object itself is neither copyable nor movable.
 *
 * \tparam dim volume-mesh dimension; only 2 and 3 are supported.
 */
template<int dim>
    requires(dim == 2 || dim == 3)
class SpaceSnapshot {
public:
    /** \brief Destroy field spaces before the supports and mesh they observe. */
    ~SpaceSnapshot();
    /** \brief Copy construction is disabled for the stable shared allocation. */
    SpaceSnapshot(const SpaceSnapshot&) = delete;
    /** \brief Copy assignment is disabled for the stable shared allocation. */
    SpaceSnapshot& operator=(const SpaceSnapshot&) = delete;
    /** \brief Move construction is disabled for the stable shared allocation. */
    SpaceSnapshot(SpaceSnapshot&&) = delete;
    /** \brief Move assignment is disabled for the stable shared allocation. */
    SpaceSnapshot& operator=(SpaceSnapshot&&) = delete;

    /** \brief Return the context-local published-space epoch. */
    [[nodiscard]] SpaceEpoch epoch() const noexcept;
    /** \brief Return the canonical field and metadata schema. */
    [[nodiscard]] const SpaceSchema& canonical_schema() const noexcept;
    /** \brief Return the complete phase-support aggregate. */
    [[nodiscard]] const PhaseSupportSet<dim>& phase_supports() const noexcept;
    /** \brief Return the complete immutable semantic state layout. */
    [[nodiscard]] const StateLayout& state_layout() const noexcept;

    /** \brief Return support-restricted spaces in canonical descriptor order. */
    [[nodiscard]] std::span<const PhaseSupportFieldGroupSpace<dim>> phase_support_field_spaces() const noexcept;
    /** \brief Return geometry spaces in canonical descriptor order. */
    [[nodiscard]] std::span<const GeometryFieldGroupSpace<dim>> geometry_field_spaces() const noexcept;
    /**
     * \brief Resolve one support-restricted field space.
     * \param id canonical category-local field-group identity.
     * \return immutable field-group space for `id`.
     * \throws std::out_of_range when `id` is invalid.
     */
    [[nodiscard]] const PhaseSupportFieldGroupSpace<dim>& phase_support_field_space(PhaseSupportFieldGroupId id) const;
    /**
     * \brief Resolve one geometry field space.
     * \param id canonical category-local field-group identity.
     * \return immutable field-group space for `id`.
     * \throws std::out_of_range when `id` is invalid.
     */
    [[nodiscard]] const GeometryFieldGroupSpace<dim>& geometry_field_space(GeometryFieldGroupId id) const;

    /** \brief Find a support field by its canonical phase and configured name. */
    [[nodiscard]] std::optional<PhaseSupportFieldGroupId>
    find_phase_support_field(PhaseId phase, std::string_view name) const noexcept;
    /** \brief Find a geometry field by its configured name. */
    [[nodiscard]] std::optional<GeometryFieldGroupId> find_geometry_field(std::string_view name) const noexcept;
    /** \brief Find discrete geometry metadata by its configured name. */
    [[nodiscard]] std::optional<DiscreteGeometryMetadataId>
    find_discrete_geometry_metadata(std::string_view name) const noexcept;
    /** \brief Find a regional scalar by its canonical phase and configured name. */
    [[nodiscard]] std::optional<RegionalEntryId> find_regional_entry(PhaseId phase,
                                                                     std::string_view name) const noexcept;

    /** \brief Create a checked reference to one support-restricted field. */
    [[nodiscard]] PhaseSupportFieldReference phase_support_field_reference(PhaseSupportFieldGroupId id) const;
    /** \brief Create a checked reference to one background geometry field. */
    [[nodiscard]] GeometryFieldReference geometry_field_reference(GeometryFieldGroupId id) const;
    /** \brief Create a checked reference to one discrete geometry-metadata block. */
    [[nodiscard]] DiscreteGeometryMetadataReference
    discrete_geometry_metadata_reference(DiscreteGeometryMetadataId id) const;
    /** \brief Create a checked reference to one nonspatial regional entry. */
    [[nodiscard]] RegionalEntryReference regional_entry_reference(RegionalEntryId id) const;

private:
    friend class RiftContext;
    friend class detail::SpaceSnapshotMakeSharedEnabler<dim>;

    /** \brief Adopt every resource transferred by successful finalization. */
    SpaceSnapshot(const RiftContext* creator_context, SpaceEpoch epoch, PhaseSupportSet<dim> phase_supports,
                  SpaceSchema schema, StateLayout layout,
                  std::unique_ptr<detail::FieldSpaceStorage<dim>> field_spaces) noexcept;

    /** \brief Non-owning identity of the context that finalized this space. */
    const RiftContext* creator_context_;

    /** \brief Published context-local space epoch. */
    SpaceEpoch epoch_;
    /** \brief Complete support aggregate retaining the mesh snapshot. */
    PhaseSupportSet<dim> phase_supports_;
    /** \brief Canonical representation-neutral schema. */
    SpaceSchema schema_;
    /** \brief Complete semantic layout. */
    StateLayout layout_;
    /** \brief Field spaces declared last so they are destroyed first. */
    std::unique_ptr<detail::FieldSpaceStorage<dim>> field_spaces_;
};

/** \brief Result of collective finalized-space publication. */
template<int dim>
    requires(dim == 2 || dim == 3)
using SpaceSnapshotResult = std::expected<std::shared_ptr<const SpaceSnapshot<dim>>, SpaceFinalizationErrors>;

} // namespace rift
