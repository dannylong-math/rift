/**
 * \file
 * \brief Collective finalization of immutable finite-element spaces.
 */

#include "field_space_storage.hpp"

#include <algorithm>
#include <array>
#include <boost/serialization/string.hpp> // IWYU pragma: keep
#include <boost/serialization/vector.hpp> // IWYU pragma: keep
#include <cstddef>
#include <cstdint>
#include <deal.II/base/array_view.h>
#include <deal.II/base/mpi.h>
#include <deal.II/base/utilities.h>
#include <expected>
#include <format>
#include <iterator>
#include <limits>
#include <memory>
#include <mpi.h>
#include <optional>
#include <rift/field_group_space.hpp>
#include <rift/phase_graph.hpp>
#include <rift/phase_support.hpp>
#include <rift/rift_context.hpp>
#include <rift/space_draft.hpp>
#include <rift/space_snapshot.hpp>
#include <simdutf.h> // NOLINT(misc-include-cleaner): simdutf's public umbrella owns this declaration.
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace rift {

namespace detail {

/** \brief Publicly constructible implementation type with access to the private snapshot constructor. */
template<int dim>
    requires(dim == 2 || dim == 3)
class SpaceSnapshotMakeSharedEnabler final : public SpaceSnapshot<dim> {
public:
    /** \brief Forward an already-agreed ownership transfer into the snapshot. */
    SpaceSnapshotMakeSharedEnabler(const RiftContext* creator_context, SpaceEpoch epoch,
                                   PhaseSupportSet<dim> phase_supports, SpaceSchema schema, StateLayout layout,
                                   std::unique_ptr<FieldSpaceStorage<dim>> field_spaces) noexcept :
        SpaceSnapshot<dim>(creator_context, epoch, std::move(phase_supports), std::move(schema), std::move(layout),
                           std::move(field_spaces))
    {
    }
};

} // namespace detail

namespace {

/** \brief Serialization-friendly canonical regional specification. */
struct RegionalEntryWire {
    /** \brief Canonical phase representation. */
    std::uint32_t phase = 0;
    /** \brief Configured regional-entry name. */
    std::string name;

    /** \brief Serialize the complete regional specification. */
    template<class Archive> void serialize(Archive& archive, const unsigned int /*version*/)
    {
        archive & phase;
        archive & name;
    }
};

/** \brief Serialization-friendly complete layout entry. */
struct LayoutEntryWire {
    /** \brief Layout-category representation. */
    std::uint8_t kind = 0;
    /** \brief Category-local strong-ID representation. */
    std::uint32_t id = 0;
    /** \brief Whether phase metadata is present. */
    bool has_phase = false;
    /** \brief Optional phase representation. */
    std::uint32_t phase = 0;
    /** \brief Canonical configured name. */
    std::string name;
    /** \brief Regional owner rank, zero for nonregional categories. */
    unsigned int owner_rank = 0;
    /** \brief Semantic block offset. */
    std::uint64_t offset = 0;
    /** \brief Logical block cardinality. */
    std::uint64_t cardinality = 0;

    /** \brief Serialize one exact canonical layout tuple. */
    template<class Archive> void serialize(Archive& archive, const unsigned int /*version*/)
    {
        archive & kind;
        archive & id;
        archive & has_phase;
        archive & phase;
        archive & name;
        archive & owner_rank;
        archive & offset;
        archive & cardinality;
    }
};

/** \brief Rank-zero-reference packet for one finalization attempt. */
struct SpaceFinalizationAgreementPacket {
    /** \brief Requested volume-mesh dimension. */
    std::uint64_t dimension = 0;
    /** \brief Whether the draft remains active. */
    bool active = false;
    /** \brief Whether the draft was created by this context. */
    bool owned_by_context = false;
    /** \brief Whether all continuous field spaces exist. */
    bool field_spaces_built = false;
    /** \brief Context-local draft epoch. */
    std::uint64_t epoch = 0;
    /** \brief Canonical regional input, retained even when validation fails. */
    std::vector<RegionalEntryWire> regional_entries;
    /** \brief Whether a complete locally valid layout follows. */
    bool has_layout = false;
    /** \brief Every layout tuple in flat semantic order. */
    std::vector<LayoutEntryWire> layout_entries;
    /** \brief Checked total layout cardinality. */
    std::uint64_t total_cardinality = 0;

    /** \brief Serialize the complete exact agreement packet. */
    template<class Archive> void serialize(Archive& archive, const unsigned int /*version*/)
    {
        archive & dimension;
        archive & active;
        archive & owned_by_context;
        archive & field_spaces_built;
        archive & epoch;
        archive & regional_entries;
        archive & has_layout;
        archive & layout_entries;
        archive & total_cardinality;
    }
};

/** \brief Serialization-friendly complete finalization diagnostic. */
struct SpaceFinalizationErrorWire {
    /** \brief Error-code representation. */
    std::uint8_t code = 0;
    /** \brief Offending world rank. */
    unsigned int rank = 0;
    /** \brief Active public-subject alternative. */
    std::uint8_t subject_kind = 0;
    /** \brief Canonical subject position. */
    std::uint64_t canonical_index = 0;
    /** \brief Complete regional subject storage. */
    RegionalEntryWire regional_entry;
    /** \brief Layout-category representation. */
    std::uint8_t layout_kind = 0;
    /** \brief Layout subject name. */
    std::string layout_name;
    /** \brief Whether the layout subject has a phase. */
    bool has_phase = false;
    /** \brief Optional layout-subject phase representation. */
    std::uint32_t phase = 0;
    /** \brief Human-readable diagnostic. */
    std::string message;

    /** \brief Serialize one complete failure-only diagnostic. */
    template<class Archive> void serialize(Archive& archive, const unsigned int /*version*/)
    {
        archive & code;
        archive & rank;
        archive & subject_kind;
        archive & canonical_index;
        archive & regional_entry;
        archive & layout_kind;
        archive & layout_name;
        archive & has_phase;
        archive & phase;
        archive & message;
    }
};

/** \brief Mutable complete layout candidate before publication. */
struct StateLayoutStorage {
    /** \brief Canonical support-field blocks. */
    std::vector<PhaseSupportFieldLayoutEntry> phase_support_fields;
    /** \brief Canonical geometry-field blocks. */
    std::vector<GeometryFieldLayoutEntry> geometry_fields;
    /** \brief Canonical discrete-metadata blocks. */
    std::vector<DiscreteGeometryMetadataLayoutEntry> discrete_geometry_metadata;
    /** \brief Canonical regional scalar blocks. */
    std::vector<RegionalEntry> regional_entries;
    /** \brief Checked total cardinality. */
    std::uint64_t total_cardinality = 0;
};

/** \brief Test whether a configured name contains well-formed UTF-8. */
[[nodiscard]] bool is_valid_utf8(const std::string_view value) noexcept
{
    // NOLINTNEXTLINE(misc-include-cleaner): declared through simdutf's public umbrella header.
    return simdutf::validate_utf8(value.data(), value.size());
}

/** \brief Convert one regional specification to its wire representation. */
[[nodiscard]] RegionalEntryWire to_wire(const RegionalEntrySpecification& specification)
{
    return {.phase = specification.phase.value(), .name = specification.name};
}

/** \brief Restore one regional specification from its wire representation. */
[[nodiscard]] RegionalEntrySpecification from_wire(RegionalEntryWire specification)
{
    return {.phase = PhaseId::from_index(specification.phase), .name = std::move(specification.name)};
}

/** \brief Put regional specifications in canonical `(PhaseId, name)` order. */
void sort_regional_entries(std::vector<RegionalEntrySpecification>& entries)
{
    std::ranges::sort(entries, [](const auto& left, const auto& right) {
        return std::tie(left.phase, left.name) < std::tie(right.phase, right.name);
    });
}

/** \brief Add one typed finalization diagnostic. */
void add_error(SpaceFinalizationErrors& errors, const SpaceFinalizationErrorCode code, const unsigned int rank,
               SpaceFinalizationErrorSubject subject, std::string message)
{
    errors.push_back({.code = code, .rank = rank, .subject = std::move(subject), .message = std::move(message)});
}

/** \brief Order diagnostics deterministically by rank, code, subject, and message. */
void sort_errors(SpaceFinalizationErrors& errors)
{
    std::ranges::sort(errors, [](const auto& left, const auto& right) {
        return std::tuple{left.rank, left.code, left.subject.index(), left.message} <
               std::tuple{right.rank, right.code, right.subject.index(), right.message};
    });
}

/** \brief Validate lifecycle and regional input without mutating the draft. */
template<int dim>
[[nodiscard]] SpaceFinalizationErrors
validate_finalization_input(const SpaceDraft<dim>& draft, const bool owned_by_context, const PhaseGraph& phase_graph,
                            const std::vector<RegionalEntrySpecification>& regional_entries, const unsigned int rank)
{
    SpaceFinalizationErrors errors;
    if (!draft.active()) {
        add_error(errors, SpaceFinalizationErrorCode::inactive_draft, rank, {},
                  std::format("rank {} supplied an inactive space draft for finalization", rank));
        return errors;
    }
    // deal.II permits only one MPI_InitFinalize owner, and RiftContext is that
    // non-movable lifetime anchor. A second live context therefore cannot be
    // constructed through the public API. The reviewer approved this narrow
    // defensive-path exclusion on 2026-09-04.
    if (!owned_by_context) { // GCOVR_EXCL_BR_LINE
        // GCOVR_EXCL_START
        add_error(errors, SpaceFinalizationErrorCode::foreign_draft, rank, {},
                  std::format("rank {} supplied a space draft created by another RiftContext", rank));
        // GCOVR_EXCL_STOP
    }
    if (!draft.field_spaces_built()) {
        add_error(errors, SpaceFinalizationErrorCode::field_spaces_not_built, rank, {},
                  std::format("rank {} supplied space draft {} before building its field spaces", rank,
                              draft.epoch().value()));
    }

    const auto phase_count = phase_graph.phases().size();
    std::size_t index = 0;
    for (auto entry = regional_entries.begin(); entry != regional_entries.end(); ++entry, ++index) {
        const auto& specification = *entry;
        const RegionalEntryErrorSubject subject{.sorted_index = index, .specification = specification};
        if (specification.name.empty()) {
            add_error(errors, SpaceFinalizationErrorCode::empty_name, rank, subject,
                      std::format("regional entry at sorted index {} has parsed name ''; expected a nonempty "
                                  "UTF-8 name unique within phase {}",
                                  index, specification.phase.value()));
        }
        if (!is_valid_utf8(specification.name)) {
            add_error(errors, SpaceFinalizationErrorCode::invalid_name_encoding, rank, subject,
                      std::format("regional entry at sorted index {} has invalid UTF-8 in parsed name; expected a "
                                  "valid UTF-8 name unique within phase {}",
                                  index, specification.phase.value()));
        }
        if (specification.phase.value() >= phase_count) {
            add_error(errors, SpaceFinalizationErrorCode::unknown_phase, rank, subject,
                      std::format("regional entry '{}' at sorted index {} has parsed phase {}; expected a canonical "
                                  "phase ID in [0, {})",
                                  specification.name, index, specification.phase.value(), phase_count));
        }
        const bool duplicate_before = entry != regional_entries.begin() &&
                                      std::prev(entry)->phase == specification.phase &&
                                      std::prev(entry)->name == specification.name;
        const bool duplicate_after = std::next(entry) != regional_entries.end() &&
                                     std::next(entry)->phase == specification.phase &&
                                     std::next(entry)->name == specification.name;
        if (duplicate_before || duplicate_after) {
            add_error(errors, SpaceFinalizationErrorCode::duplicate_name, rank, subject,
                      std::format("regional entry at sorted index {} has parsed phase {} and name '{}'; expected a "
                                  "name unique within that phase",
                                  index, specification.phase.value(), specification.name));
        }
    }
    return errors;
}

/** \brief Rank data needed while constructing one local layout. */
struct FinalizationLocation {
    /** \brief Calling world rank. */
    unsigned int rank;
    /** \brief Positive world-communicator size. */
    unsigned int rank_count;
};

/** \brief Create a typed subject for one canonical layout block. */
[[nodiscard]] LayoutEntryErrorSubject make_layout_subject(const LayoutEntryKind kind, const std::size_t index,
                                                          std::string name,
                                                          const std::optional<PhaseId> phase = std::nullopt)
{
    return {.kind = kind, .canonical_index = index, .name = std::move(name), .phase = phase};
}

/** \brief Append checked block cardinality to the running semantic offset. */
void checked_advance(std::uint64_t& offset, const std::uint64_t cardinality, const LayoutEntryErrorSubject& subject,
                     const unsigned int rank, SpaceFinalizationErrors& errors)
{
    // Reaching this guard requires collectively constructed deal.II spaces
    // whose aggregate global DoF count exceeds uint64_t. Such a candidate
    // cannot be constructed within the addressable resources of the supported
    // process model. The reviewer approved this exclusion on 2026-09-04.
    if (cardinality > std::numeric_limits<std::uint64_t>::max() - offset) { // GCOVR_EXCL_BR_LINE
        // GCOVR_EXCL_START
        add_error(errors, SpaceFinalizationErrorCode::layout_overflow, rank, subject,
                  std::format("layout block '{}' at canonical index {} with offset {} and cardinality {} exceeds "
                              "the uint64_t layout range",
                              subject.name, subject.canonical_index, offset, cardinality));
        return;
        // GCOVR_EXCL_STOP
    }
    offset += cardinality;
}

/** \brief Append all support-field blocks to one layout candidate. */
template<int dim>
void append_phase_support_fields(const SpaceDraft<dim>& draft, StateLayoutStorage& storage, std::uint64_t& offset,
                                 const FinalizationLocation location, SpaceFinalizationErrors& errors)
{
    const auto phase_descriptors = draft.canonical_schema().phase_support_fields();
    const auto phase_spaces = draft.phase_support_field_spaces();
    storage.phase_support_fields.reserve(phase_descriptors.size());
    std::size_t index = 0;
    auto space = phase_spaces.begin();
    for (const auto& descriptor : phase_descriptors) {
        const auto subject =
            make_layout_subject(LayoutEntryKind::phase_support_field, index, descriptor.name, descriptor.phase);
        const auto cardinality = static_cast<std::uint64_t>(space->dof_handler().n_dofs());
        storage.phase_support_fields.push_back(
            {.field_group = descriptor.id, .offset = offset, .cardinality = cardinality});
        checked_advance(offset, cardinality, subject, location.rank, errors);
        ++space;
        ++index;
    }
}

/** \brief Append all continuous geometry-field blocks to one candidate. */
template<int dim>
void append_geometry_fields(const SpaceDraft<dim>& draft, StateLayoutStorage& storage, std::uint64_t& offset,
                            const FinalizationLocation location, SpaceFinalizationErrors& errors)
{
    const auto geometry_descriptors = draft.canonical_schema().geometry_fields();
    const auto geometry_spaces = draft.geometry_field_spaces();
    storage.geometry_fields.reserve(geometry_descriptors.size());
    std::size_t index = 0;
    auto space = geometry_spaces.begin();
    for (const auto& descriptor : geometry_descriptors) {
        const auto subject = make_layout_subject(LayoutEntryKind::geometry_field, index, descriptor.name);
        const auto cardinality = static_cast<std::uint64_t>(space->dof_handler().n_dofs());
        storage.geometry_fields.push_back({.field_group = descriptor.id, .offset = offset, .cardinality = cardinality});
        checked_advance(offset, cardinality, subject, location.rank, errors);
        ++space;
        ++index;
    }
}

/** \brief Append all component-aligned metadata blocks to one candidate. */
template<int dim>
void append_discrete_geometry_metadata(const SpaceDraft<dim>& draft, StateLayoutStorage& storage, std::uint64_t& offset,
                                       const FinalizationLocation location, SpaceFinalizationErrors& errors)
{
    const auto metadata_descriptors = draft.canonical_schema().discrete_geometry_metadata();
    storage.discrete_geometry_metadata.reserve(metadata_descriptors.size());
    std::size_t index = 0;
    for (const auto& descriptor : metadata_descriptors) {
        const auto subject = make_layout_subject(LayoutEntryKind::discrete_geometry_metadata, index, descriptor.name);
        const auto& geometry_space = draft.geometry_field_space(descriptor.geometry_field_group);
        const auto components = geometry_space.finite_element().n_components();
        const auto geometry_cardinality = static_cast<std::uint64_t>(geometry_space.dof_handler().n_dofs());
        const auto cardinality = geometry_cardinality / components;
        storage.discrete_geometry_metadata.push_back(
            {.metadata = descriptor.id, .offset = offset, .cardinality = cardinality});
        checked_advance(offset, cardinality, subject, location.rank, errors);
        ++index;
    }
}

/** \brief Append all canonical nonspatial regional scalars to one candidate. */
void append_regional_entries(const std::vector<RegionalEntrySpecification>& regional_entries,
                             StateLayoutStorage& storage, std::uint64_t& offset, const FinalizationLocation location,
                             SpaceFinalizationErrors& errors)
{
    storage.regional_entries.reserve(regional_entries.size());
    std::size_t index = 0;
    for (const auto& specification : regional_entries) {
        const auto subject =
            make_layout_subject(LayoutEntryKind::regional_scalar, index, specification.name, specification.phase);
        // Exercising this guard would require materializing more than 2^32
        // regional specifications, including their owned strings, in one
        // process. The reviewer approved this resource-bound exclusion on
        // 2026-09-04.
        if (index > std::numeric_limits<RegionalEntryId::representation_type>::max()) { // GCOVR_EXCL_BR_LINE
            // GCOVR_EXCL_START
            add_error(errors, SpaceFinalizationErrorCode::layout_overflow, location.rank, subject,
                      std::format("regional entry '{}' at canonical index {} exceeds the regional-ID range",
                                  specification.name, index));
            continue;
            // GCOVR_EXCL_STOP
        }
        const auto id = RegionalEntryId::from_index(static_cast<RegionalEntryId::representation_type>(index));
        storage.regional_entries.push_back({.id = id,
                                            .phase = specification.phase,
                                            .name = specification.name,
                                            .owner_rank = id.value() % location.rank_count,
                                            .offset = offset,
                                            .cardinality = 1});
        checked_advance(offset, 1, subject, location.rank, errors);
        ++index;
    }
}

/** \brief Build the complete local layout, returning no partial candidate on failure. */
template<int dim>
void build_state_layout(const SpaceDraft<dim>& draft, const std::vector<RegionalEntrySpecification>& regional_entries,
                        const FinalizationLocation location, StateLayoutStorage& storage,
                        SpaceFinalizationErrors& errors)
{
    std::uint64_t offset = 0;
    append_phase_support_fields(draft, storage, offset, location, errors);
    append_geometry_fields(draft, storage, offset, location, errors);
    append_discrete_geometry_metadata(draft, storage, offset, location, errors);
    append_regional_entries(regional_entries, storage, offset, location, errors);
    storage.total_cardinality = offset;
}

/** \brief Convert a complete layout candidate to exact agreement tuples. */
[[nodiscard]] std::vector<LayoutEntryWire> to_wire(const StateLayoutStorage& storage, const SpaceSchema& schema)
{
    std::vector<LayoutEntryWire> entries;
    entries.reserve(storage.phase_support_fields.size() + storage.geometry_fields.size() +
                    storage.discrete_geometry_metadata.size() + storage.regional_entries.size());

    const auto phase_descriptors = schema.phase_support_fields();
    for (const auto& entry : storage.phase_support_fields) {
        const auto descriptor = std::ranges::find_if(
            phase_descriptors, [&entry](const auto& value) { return value.id == entry.field_group; });
        entries.push_back({.kind = static_cast<std::uint8_t>(LayoutEntryKind::phase_support_field),
                           .id = entry.field_group.value(),
                           .has_phase = true,
                           .phase = descriptor->phase.value(),
                           .name = descriptor->name,
                           .owner_rank = 0,
                           .offset = entry.offset,
                           .cardinality = entry.cardinality});
    }
    const auto geometry_descriptors = schema.geometry_fields();
    for (const auto& entry : storage.geometry_fields) {
        const auto descriptor = std::ranges::find_if(
            geometry_descriptors, [&entry](const auto& value) { return value.id == entry.field_group; });
        entries.push_back({.kind = static_cast<std::uint8_t>(LayoutEntryKind::geometry_field),
                           .id = entry.field_group.value(),
                           .has_phase = false,
                           .phase = 0,
                           .name = descriptor->name,
                           .owner_rank = 0,
                           .offset = entry.offset,
                           .cardinality = entry.cardinality});
    }
    const auto metadata_descriptors = schema.discrete_geometry_metadata();
    for (const auto& entry : storage.discrete_geometry_metadata) {
        const auto descriptor = std::ranges::find_if(
            metadata_descriptors, [&entry](const auto& value) { return value.id == entry.metadata; });
        entries.push_back({.kind = static_cast<std::uint8_t>(LayoutEntryKind::discrete_geometry_metadata),
                           .id = entry.metadata.value(),
                           .has_phase = false,
                           .phase = 0,
                           .name = descriptor->name,
                           .owner_rank = 0,
                           .offset = entry.offset,
                           .cardinality = entry.cardinality});
    }
    for (const auto& entry : storage.regional_entries) {
        entries.push_back({.kind = static_cast<std::uint8_t>(LayoutEntryKind::regional_scalar),
                           .id = entry.id.value(),
                           .has_phase = true,
                           .phase = entry.phase.value(),
                           .name = entry.name,
                           .owner_rank = entry.owner_rank,
                           .offset = entry.offset,
                           .cardinality = entry.cardinality});
    }
    return entries;
}

/** \brief Build the complete exact rank-agreement packet. */
template<int dim>
[[nodiscard]] SpaceFinalizationAgreementPacket
make_agreement_packet(const SpaceDraft<dim>& draft, const std::vector<RegionalEntrySpecification>& regional_entries,
                      const StateLayoutStorage& layout, const bool has_layout, const bool owned_by_context)
{
    SpaceFinalizationAgreementPacket packet{.dimension = dim,
                                            .active = draft.active(),
                                            .owned_by_context = owned_by_context,
                                            .field_spaces_built = draft.field_spaces_built(),
                                            .epoch = draft.epoch().value(),
                                            .regional_entries = {},
                                            .has_layout = has_layout,
                                            .layout_entries = {},
                                            .total_cardinality =
                                                has_layout ? layout.total_cardinality : std::uint64_t{0}};
    packet.regional_entries.reserve(regional_entries.size());
    std::ranges::transform(regional_entries, std::back_inserter(packet.regional_entries),
                           [](const auto& specification) { return to_wire(specification); });
    if (has_layout) {
        packet.layout_entries = to_wire(layout, draft.canonical_schema());
    }
    return packet;
}

/** \brief Compare exact regional wire vectors without defining public equality. */
[[nodiscard]] bool equivalent_regional_schema(const std::vector<RegionalEntryWire>& left,
                                              const std::vector<RegionalEntryWire>& right)
{
    return dealii::Utilities::pack(left, false) == dealii::Utilities::pack(right, false);
}

/** \brief Compare exact layout wire tuples without defining public equality. */
[[nodiscard]] bool equivalent_layout(const SpaceFinalizationAgreementPacket& left,
                                     const SpaceFinalizationAgreementPacket& right)
{
    if (left.dimension != right.dimension) {
        return false;
    }
    if (left.has_layout != right.has_layout) {
        return false;
    }
    if (left.total_cardinality != right.total_cardinality) {
        return false;
    }
    const auto left_layout = dealii::Utilities::pack(left.layout_entries, false);
    const auto right_layout = dealii::Utilities::pack(right.layout_entries, false);
    return left_layout == right_layout;
}

/** \brief Add exact rank-zero-reference mismatch diagnostics. */
void add_agreement_errors(const SpaceFinalizationAgreementPacket& local,
                          const SpaceFinalizationAgreementPacket& reference, const unsigned int rank,
                          SpaceFinalizationErrors& errors)
{
    if (std::tuple{local.active, local.owned_by_context, local.field_spaces_built} !=
        std::tuple{reference.active, reference.owned_by_context, reference.field_spaces_built}) {
        add_error(errors, SpaceFinalizationErrorCode::collective_draft_state_mismatch, rank, {},
                  std::format("rank {} supplied draft state active={}, owned_by_context={}, field_spaces_built={}; "
                              "rank 0 supplied active={}, owned_by_context={}, field_spaces_built={}",
                              rank, local.active, local.owned_by_context, local.field_spaces_built, reference.active,
                              reference.owned_by_context, reference.field_spaces_built));
    }
    if (local.epoch != reference.epoch) {
        add_error(
            errors, SpaceFinalizationErrorCode::collective_space_epoch_mismatch, rank, {},
            std::format("rank {} supplied space epoch {}, but rank 0 supplied {}", rank, local.epoch, reference.epoch));
    }
    if (!equivalent_regional_schema(local.regional_entries, reference.regional_entries)) {
        add_error(errors, SpaceFinalizationErrorCode::regional_schema_mismatch, rank, {},
                  std::format("rank {} canonical regional schema differs from rank 0", rank));
    }
    if (!equivalent_layout(local, reference)) {
        add_error(errors, SpaceFinalizationErrorCode::layout_mismatch, rank, {},
                  std::format("rank {} complete state layout differs from rank 0", rank));
    }
}

/** \brief Convert one typed public error to its failure-only wire form. */
[[nodiscard]] SpaceFinalizationErrorWire to_wire(SpaceFinalizationError error)
{
    SpaceFinalizationErrorWire wire{.code = static_cast<std::uint8_t>(error.code),
                                    .rank = error.rank,
                                    .subject_kind = static_cast<std::uint8_t>(error.subject.index()),
                                    .canonical_index = 0,
                                    .regional_entry = {},
                                    .layout_kind = 0,
                                    .layout_name = {},
                                    .has_phase = false,
                                    .phase = 0,
                                    .message = std::move(error.message)};
    if (const auto* subject = std::get_if<RegionalEntryErrorSubject>(&error.subject)) {
        wire.canonical_index = subject->sorted_index;
        wire.regional_entry = to_wire(subject->specification);
    }
    // Layout subjects currently arise only from the two approved resource-
    // bound overflow guards above. The reviewer approved excluding their
    // failure-only wire conversion on 2026-09-04.
    else if (const auto* subject = std::get_if<LayoutEntryErrorSubject>(&error.subject)) { // GCOVR_EXCL_BR_LINE
        // GCOVR_EXCL_START
        wire.canonical_index = subject->canonical_index;
        wire.layout_kind = static_cast<std::uint8_t>(subject->kind);
        wire.layout_name = subject->name;
        wire.has_phase = subject->phase.has_value();
        wire.phase = subject->phase.has_value() ? subject->phase->value() : std::uint32_t{0};
        // GCOVR_EXCL_STOP
    }
    return wire;
}

/** \brief Restore one typed public error from its failure-only wire form. */
[[nodiscard]] SpaceFinalizationError from_wire(SpaceFinalizationErrorWire wire)
{
    SpaceFinalizationErrorSubject subject;
    if (wire.subject_kind == 1) {
        subject = RegionalEntryErrorSubject{.sorted_index = wire.canonical_index,
                                            .specification = from_wire(std::move(wire.regional_entry))};
    }
    // This alternative is the inverse of the approved overflow-only wire
    // conversion above and has the same reviewer-approved exclusion.
    else if (wire.subject_kind == 2) { // GCOVR_EXCL_BR_LINE
        // GCOVR_EXCL_START
        subject = LayoutEntryErrorSubject{.kind = static_cast<LayoutEntryKind>(wire.layout_kind),
                                          .canonical_index = wire.canonical_index,
                                          .name = std::move(wire.layout_name),
                                          .phase = wire.has_phase ? std::optional{PhaseId::from_index(wire.phase)}
                                                                  : std::nullopt};
        // GCOVR_EXCL_STOP
    }
    return {.code = static_cast<SpaceFinalizationErrorCode>(wire.code),
            .rank = wire.rank,
            .subject = std::move(subject),
            .message = std::move(wire.message)};
}

/** \brief Gather complete typed diagnostics only after collective failure. */
[[nodiscard]] SpaceFinalizationErrors collect_failure_errors(const MPI_Comm communicator,
                                                             SpaceFinalizationErrors local_errors)
{
    std::vector<SpaceFinalizationErrorWire> local_wire;
    local_wire.reserve(local_errors.size());
    std::ranges::transform(local_errors, std::back_inserter(local_wire),
                           [](auto& error) { return to_wire(std::move(error)); });
    const auto gathered = dealii::Utilities::MPI::all_gather(communicator, local_wire);

    SpaceFinalizationErrors errors;
    for (const auto& rank_errors : gathered) {
        std::ranges::transform(rank_errors, std::back_inserter(errors),
                               [](auto error) { return from_wire(std::move(error)); });
    }
    sort_errors(errors);
    return errors;
}

} // namespace

StateLayout::StateLayout(std::vector<PhaseSupportFieldLayoutEntry> phase_support_fields,
                         std::vector<GeometryFieldLayoutEntry> geometry_fields,
                         std::vector<DiscreteGeometryMetadataLayoutEntry> discrete_geometry_metadata,
                         std::vector<RegionalEntry> regional_entries, const std::uint64_t total_cardinality) noexcept :
    phase_support_fields_(std::move(phase_support_fields)),
    geometry_fields_(std::move(geometry_fields)),
    discrete_geometry_metadata_(std::move(discrete_geometry_metadata)),
    regional_entries_(std::move(regional_entries)),
    total_cardinality_(total_cardinality)
{
}

std::span<const PhaseSupportFieldLayoutEntry> StateLayout::phase_support_fields() const noexcept
{
    return phase_support_fields_;
}

std::span<const GeometryFieldLayoutEntry> StateLayout::geometry_fields() const noexcept { return geometry_fields_; }

std::span<const DiscreteGeometryMetadataLayoutEntry> StateLayout::discrete_geometry_metadata() const noexcept
{
    return discrete_geometry_metadata_;
}

std::span<const RegionalEntry> StateLayout::regional_entries() const noexcept { return regional_entries_; }

std::uint64_t StateLayout::total_cardinality() const noexcept { return total_cardinality_; }

const PhaseSupportFieldLayoutEntry& StateLayout::phase_support_field(const PhaseSupportFieldGroupId id) const
{
    if (id.value() >= phase_support_fields_.size()) {
        throw std::out_of_range(
            std::format("phase-support layout ID {} is outside [0, {})", id.value(), phase_support_fields_.size()));
    }
    return phase_support_fields_.at(id.value());
}

const GeometryFieldLayoutEntry& StateLayout::geometry_field(const GeometryFieldGroupId id) const
{
    if (id.value() >= geometry_fields_.size()) {
        throw std::out_of_range(
            std::format("geometry layout ID {} is outside [0, {})", id.value(), geometry_fields_.size()));
    }
    return geometry_fields_.at(id.value());
}

const DiscreteGeometryMetadataLayoutEntry&
StateLayout::discrete_geometry_metadata(const DiscreteGeometryMetadataId id) const
{
    if (id.value() >= discrete_geometry_metadata_.size()) {
        throw std::out_of_range(std::format("discrete geometry-metadata layout ID {} is outside [0, {})", id.value(),
                                            discrete_geometry_metadata_.size()));
    }
    return discrete_geometry_metadata_.at(id.value());
}

const RegionalEntry& StateLayout::regional_entry(const RegionalEntryId id) const
{
    if (id.value() >= regional_entries_.size()) {
        throw std::out_of_range(
            std::format("regional layout ID {} is outside [0, {})", id.value(), regional_entries_.size()));
    }
    return regional_entries_.at(id.value());
}

template<int dim>
    requires(dim == 2 || dim == 3)
SpaceSnapshot<dim>::SpaceSnapshot(const RiftContext* creator_context, const SpaceEpoch epoch,
                                  PhaseSupportSet<dim> phase_supports, SpaceSchema schema, StateLayout layout,
                                  std::unique_ptr<detail::FieldSpaceStorage<dim>> field_spaces) noexcept :
    creator_context_(creator_context),
    epoch_(epoch),
    phase_supports_(std::move(phase_supports)),
    schema_(std::move(schema)),
    layout_(std::move(layout)),
    field_spaces_(std::move(field_spaces))
{
}

template<int dim>
    requires(dim == 2 || dim == 3)
SpaceSnapshot<dim>::~SpaceSnapshot() = default;

template<int dim>
    requires(dim == 2 || dim == 3)
SpaceEpoch SpaceSnapshot<dim>::epoch() const noexcept
{
    return epoch_;
}

template<int dim>
    requires(dim == 2 || dim == 3)
const SpaceSchema& SpaceSnapshot<dim>::canonical_schema() const noexcept
{
    return schema_;
}

template<int dim>
    requires(dim == 2 || dim == 3)
const PhaseSupportSet<dim>& SpaceSnapshot<dim>::phase_supports() const noexcept
{
    return phase_supports_;
}

template<int dim>
    requires(dim == 2 || dim == 3)
const StateLayout& SpaceSnapshot<dim>::state_layout() const noexcept
{
    return layout_;
}

template<int dim>
    requires(dim == 2 || dim == 3)
std::span<const PhaseSupportFieldGroupSpace<dim>> SpaceSnapshot<dim>::phase_support_field_spaces() const noexcept
{
    return field_spaces_->phase_support_fields;
}

template<int dim>
    requires(dim == 2 || dim == 3)
std::span<const GeometryFieldGroupSpace<dim>> SpaceSnapshot<dim>::geometry_field_spaces() const noexcept
{
    return field_spaces_->geometry_fields;
}

template<int dim>
    requires(dim == 2 || dim == 3)
const PhaseSupportFieldGroupSpace<dim>&
SpaceSnapshot<dim>::phase_support_field_space(const PhaseSupportFieldGroupId id) const
{
    if (id.value() >= field_spaces_->phase_support_fields.size()) {
        throw std::out_of_range(std::format("phase-support field-group ID {} is outside [0, {})", id.value(),
                                            field_spaces_->phase_support_fields.size()));
    }
    return field_spaces_->phase_support_fields.at(id.value());
}

template<int dim>
    requires(dim == 2 || dim == 3)
const GeometryFieldGroupSpace<dim>& SpaceSnapshot<dim>::geometry_field_space(const GeometryFieldGroupId id) const
{
    if (id.value() >= field_spaces_->geometry_fields.size()) {
        throw std::out_of_range(std::format("geometry field-group ID {} is outside [0, {})", id.value(),
                                            field_spaces_->geometry_fields.size()));
    }
    return field_spaces_->geometry_fields.at(id.value());
}

template<int dim>
    requires(dim == 2 || dim == 3)
std::optional<PhaseSupportFieldGroupId>
SpaceSnapshot<dim>::find_phase_support_field(const PhaseId phase, const std::string_view name) const noexcept
{
    const auto descriptors = schema_.phase_support_fields();
    const auto found = std::ranges::find_if(descriptors, [phase, name](const auto& descriptor) {
        return descriptor.phase == phase && descriptor.name == name;
    });
    return found == descriptors.end() ? std::nullopt : std::optional{found->id};
}

template<int dim>
    requires(dim == 2 || dim == 3)
std::optional<GeometryFieldGroupId> SpaceSnapshot<dim>::find_geometry_field(const std::string_view name) const noexcept
{
    const auto descriptors = schema_.geometry_fields();
    const auto found =
        std::ranges::find_if(descriptors, [name](const auto& descriptor) { return descriptor.name == name; });
    return found == descriptors.end() ? std::nullopt : std::optional{found->id};
}

template<int dim>
    requires(dim == 2 || dim == 3)
std::optional<DiscreteGeometryMetadataId>
SpaceSnapshot<dim>::find_discrete_geometry_metadata(const std::string_view name) const noexcept
{
    const auto descriptors = schema_.discrete_geometry_metadata();
    const auto found =
        std::ranges::find_if(descriptors, [name](const auto& descriptor) { return descriptor.name == name; });
    return found == descriptors.end() ? std::nullopt : std::optional{found->id};
}

template<int dim>
    requires(dim == 2 || dim == 3)
std::optional<RegionalEntryId> SpaceSnapshot<dim>::find_regional_entry(const PhaseId phase,
                                                                       const std::string_view name) const noexcept
{
    const auto entries = layout_.regional_entries();
    const auto found = std::ranges::find_if(
        entries, [phase, name](const auto& entry) { return entry.phase == phase && entry.name == name; });
    return found == entries.end() ? std::nullopt : std::optional{found->id};
}

template<int dim>
    requires(dim == 2 || dim == 3)
PhaseSupportFieldReference SpaceSnapshot<dim>::phase_support_field_reference(const PhaseSupportFieldGroupId id) const
{
    static_cast<void>(phase_support_field_space(id));
    return {.space_epoch = epoch_, .field_group = id};
}

template<int dim>
    requires(dim == 2 || dim == 3)
GeometryFieldReference SpaceSnapshot<dim>::geometry_field_reference(const GeometryFieldGroupId id) const
{
    static_cast<void>(geometry_field_space(id));
    return {.space_epoch = epoch_, .field_group = id};
}

template<int dim>
    requires(dim == 2 || dim == 3)
DiscreteGeometryMetadataReference
SpaceSnapshot<dim>::discrete_geometry_metadata_reference(const DiscreteGeometryMetadataId id) const
{
    static_cast<void>(layout_.discrete_geometry_metadata(id));
    return {.space_epoch = epoch_, .metadata = id};
}

template<int dim>
    requires(dim == 2 || dim == 3)
RegionalEntryReference SpaceSnapshot<dim>::regional_entry_reference(const RegionalEntryId id) const
{
    static_cast<void>(layout_.regional_entry(id));
    return {.space_epoch = epoch_, .regional_entry = id};
}

template<int dim>
    requires(dim == 2 || dim == 3)
SpaceSnapshotResult<dim> RiftContext::finalize_space(SpaceDraft<dim>& draft,
                                                     std::vector<RegionalEntrySpecification> regional_entries)
{
    sort_regional_entries(regional_entries);
    const bool owned_by_context = draft.creator_context_ == this;
    auto local_errors =
        validate_finalization_input(draft, owned_by_context, *phase_graph_, regional_entries, this_mpi_process());

    StateLayoutStorage layout;
    bool has_layout = false;
    if (local_errors.empty()) {
        build_state_layout(draft, regional_entries,
                           FinalizationLocation{.rank = this_mpi_process(), .rank_count = n_mpi_processes()}, layout,
                           local_errors);
        has_layout = local_errors.empty();
    }
    const auto local_packet = make_agreement_packet(draft, regional_entries, layout, has_layout, owned_by_context);
    const auto reference_packet = dealii::Utilities::MPI::broadcast(mpi_communicator(), local_packet, 0);
    const auto local_packet_bytes = dealii::Utilities::pack(local_packet, false);
    const auto reference_packet_bytes = dealii::Utilities::pack(reference_packet, false);
    const std::array<unsigned int, 2> local_flags{{
        static_cast<unsigned int>(has_layout),
        static_cast<unsigned int>(local_packet_bytes == reference_packet_bytes),
    }};
    std::array<unsigned int, 2> global_flags{};
    dealii::Utilities::MPI::min(dealii::make_array_view(local_flags), mpi_communicator(),
                                dealii::make_array_view(global_flags));

    if (global_flags.at(0) == 0 || global_flags.at(1) == 0) {
        add_agreement_errors(local_packet, reference_packet, this_mpi_process(), local_errors);
        sort_errors(local_errors);
        return std::unexpected(collect_failure_errors(mpi_communicator(), std::move(local_errors)));
    }

    StateLayout published_layout(std::move(layout.phase_support_fields), std::move(layout.geometry_fields),
                                 std::move(layout.discrete_geometry_metadata), std::move(layout.regional_entries),
                                 layout.total_cardinality);
    auto mutable_snapshot = std::make_shared<detail::SpaceSnapshotMakeSharedEnabler<dim>>(
        this, draft.epoch_, std::move(draft.phase_supports_), std::move(draft.schema_), std::move(published_layout),
        std::move(draft.field_spaces_));
    std::shared_ptr<const SpaceSnapshot<dim>> snapshot = std::move(mutable_snapshot);
    draft.creator_context_ = nullptr;
    draft.active_ = false;
    return snapshot;
}

template class SpaceSnapshot<2>;
template class SpaceSnapshot<3>;

template SpaceSnapshotResult<2> RiftContext::finalize_space<2>(SpaceDraft<2>&, std::vector<RegionalEntrySpecification>);
template SpaceSnapshotResult<3> RiftContext::finalize_space<3>(SpaceDraft<3>&, std::vector<RegionalEntrySpecification>);

} // namespace rift
