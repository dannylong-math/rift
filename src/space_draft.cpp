/**
 * \file
 * \brief Collective validation and construction of canonical space drafts.
 */

#include <algorithm>
#include <array>
#include <boost/serialization/array.hpp>  // IWYU pragma: keep
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
#include <mpi.h>
#include <optional>
#include <rift/field_group_space.hpp>
#include <rift/phase_graph.hpp>
#include <rift/phase_support.hpp>
#include <rift/rift_context.hpp>
#include <rift/space_draft.hpp>
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

/** \brief Own both canonical field-space categories after atomic commit. */
template<int dim>
    requires(dim == 2 || dim == 3)
struct FieldSpaceStorage {
    /** \brief Support-restricted spaces in canonical descriptor order. */
    std::vector<PhaseSupportFieldGroupSpace<dim>> phase_support_fields;
    /** \brief Geometry spaces in canonical descriptor order. */
    std::vector<GeometryFieldGroupSpace<dim>> geometry_fields;
};

} // namespace detail

namespace {

/** \brief Fixed rank-local summary for field-space construction preflight. */
using FieldSpacePreflightRecord = std::array<std::uint64_t, 4>;

/** \brief Position of each value in a field-space preflight record. */
enum class FieldSpacePreflightField : std::uint8_t {
    active,
    built,
    epoch,
    numbering,
};

static_assert(std::tuple_size_v<FieldSpacePreflightRecord> ==
              static_cast<std::size_t>(FieldSpacePreflightField::numbering) + 1);

/** \brief Convert a preflight field to its fixed record index. */
[[nodiscard]] constexpr std::size_t preflight_index(const FieldSpacePreflightField field) noexcept
{
    return static_cast<std::size_t>(field);
}

/** \brief Read one boolean field from a fixed preflight record. */
[[nodiscard]] bool preflight_boolean(const FieldSpacePreflightRecord& record,
                                     const FieldSpacePreflightField field) noexcept
{
    return record.at(preflight_index(field)) != 0;
}

/** \brief Name a stored numbering representation for a diagnostic. */
[[nodiscard]] std::string format_numbering(const std::uint64_t numbering)
{
    if (numbering == static_cast<std::uint64_t>(DofNumbering::native)) {
        return "native";
    }
    if (numbering == static_cast<std::uint64_t>(DofNumbering::component_wise)) {
        return "component_wise";
    }
    return std::format("unknown ({})", numbering);
}

/** \brief Append one typed field-space preflight diagnostic. */
void add_field_space_error(FieldSpaceBuildErrors& errors, const FieldSpaceBuildErrorCode code, const unsigned int rank,
                           std::string message)
{
    errors.push_back({.code = code, .rank = rank, .message = std::move(message)});
}

/** \brief Order field-space diagnostics by rank, code, then message. */
void sort_field_space_errors(FieldSpaceBuildErrors& errors)
{
    std::ranges::sort(errors, [](const FieldSpaceBuildError& left, const FieldSpaceBuildError& right) {
        return std::tie(left.rank, left.code, left.message) < std::tie(right.rank, right.code, right.message);
    });
}

/** \brief Describe one draft and numbering request with constant-size data. */
template<int dim>
[[nodiscard]] FieldSpacePreflightRecord make_field_space_preflight_record(const SpaceDraft<dim>& draft,
                                                                          const DofNumbering numbering)
{
    return FieldSpacePreflightRecord{{
        static_cast<std::uint64_t>(draft.active()),
        static_cast<std::uint64_t>(draft.field_spaces_built()),
        draft.epoch().value(),
        static_cast<std::uint64_t>(numbering),
    }};
}

/** \brief Derive complete deterministic diagnostics after failed preflight. */
[[nodiscard]] FieldSpaceBuildErrors collect_field_space_preflight_errors(const MPI_Comm communicator,
                                                                         const FieldSpacePreflightRecord& local_record)
{
    const auto records = dealii::Utilities::MPI::all_gather(communicator, local_record);
    const auto& reference = records.front();
    FieldSpaceBuildErrors errors;

    for (std::size_t rank_index = 0; rank_index < records.size(); ++rank_index) {
        const auto rank = static_cast<unsigned int>(rank_index);
        const auto& record = records.at(rank_index);
        const auto active = preflight_boolean(record, FieldSpacePreflightField::active);
        const auto built = preflight_boolean(record, FieldSpacePreflightField::built);

        if (!active) {
            add_field_space_error(errors, FieldSpaceBuildErrorCode::inactive_draft, rank,
                                  std::format("rank {} supplied an inactive or moved-from space draft", rank));
        }
        if (built) {
            add_field_space_error(errors, FieldSpaceBuildErrorCode::field_spaces_already_built, rank,
                                  std::format("rank {} supplied a draft whose field spaces are already built", rank));
        }

        if (active != preflight_boolean(reference, FieldSpacePreflightField::active) ||
            built != preflight_boolean(reference, FieldSpacePreflightField::built)) {
            add_field_space_error(
                errors, FieldSpaceBuildErrorCode::collective_draft_state_mismatch, rank,
                std::format("rank {} supplied draft state {{active={}, built={}}}, but rank 0 supplied "
                            "{{active={}, built={}}}",
                            rank, active, built, preflight_boolean(reference, FieldSpacePreflightField::active),
                            preflight_boolean(reference, FieldSpacePreflightField::built)));
        }

        const auto epoch = record.at(preflight_index(FieldSpacePreflightField::epoch));
        const auto reference_epoch = reference.at(preflight_index(FieldSpacePreflightField::epoch));
        if (epoch != reference_epoch) {
            add_field_space_error(
                errors, FieldSpaceBuildErrorCode::collective_space_epoch_mismatch, rank,
                std::format("rank {} supplied space epoch {}, but rank 0 supplied {}", rank, epoch, reference_epoch));
        }

        const auto numbering = record.at(preflight_index(FieldSpacePreflightField::numbering));
        const auto reference_numbering = reference.at(preflight_index(FieldSpacePreflightField::numbering));
        if (numbering != reference_numbering) {
            add_field_space_error(errors, FieldSpaceBuildErrorCode::collective_dof_numbering_mismatch, rank,
                                  std::format("rank {} requested DoF numbering '{}', but rank 0 requested '{}'", rank,
                                              format_numbering(numbering), format_numbering(reference_numbering)));
        }
    }

    sort_field_space_errors(errors);
    return errors;
}

/** \brief Enforce the fixed collective contract before entering deal.II. */
template<int dim>
[[nodiscard]] FieldSpaceBuildResult
preflight_field_space_build(const MPI_Comm communicator, const SpaceDraft<dim>& draft, const DofNumbering numbering)
{
    const auto local_record = make_field_space_preflight_record(draft, numbering);
    const auto reference = dealii::Utilities::MPI::broadcast(communicator, local_record, 0);
    const auto local_state = std::tuple{local_record.at(preflight_index(FieldSpacePreflightField::active)),
                                        local_record.at(preflight_index(FieldSpacePreflightField::built))};
    const auto reference_state = std::tuple{reference.at(preflight_index(FieldSpacePreflightField::active)),
                                            reference.at(preflight_index(FieldSpacePreflightField::built))};
    const std::array<unsigned int, 4> local_flags{{
        static_cast<unsigned int>(draft.active() && !draft.field_spaces_built()),
        static_cast<unsigned int>(local_state == reference_state),
        static_cast<unsigned int>(local_record.at(preflight_index(FieldSpacePreflightField::epoch)) ==
                                  reference.at(preflight_index(FieldSpacePreflightField::epoch))),
        static_cast<unsigned int>(local_record.at(preflight_index(FieldSpacePreflightField::numbering)) ==
                                  reference.at(preflight_index(FieldSpacePreflightField::numbering))),
    }};
    std::array<unsigned int, 4> global_flags{};
    dealii::Utilities::MPI::min(dealii::make_array_view(local_flags), communicator,
                                dealii::make_array_view(global_flags));

    if (std::ranges::any_of(global_flags, [](const unsigned int flag) { return flag == 0; })) {
        return std::unexpected(collect_field_space_preflight_errors(communicator, local_record));
    }
    return {};
}

/** \brief Serialization-friendly form of geometry component semantics. */
struct GeometryFieldComponentsWire {
    /** \brief Zero for unbound components and one for phase-bound components. */
    std::uint8_t kind = 0;
    /** \brief Unbound component count. */
    unsigned int count = 0;
    /** \brief Phase IDs for phase-bound components. */
    std::vector<std::uint32_t> phases;

    /** \brief Serialize exact component semantics. */
    template<class Archive> void serialize(Archive& archive, const unsigned int /*version*/)
    {
        archive & kind;
        archive & count;
        archive & phases;
    }
};

/** \brief Serialization-friendly support-field specification. */
struct PhaseSupportFieldGroupWire {
    /** \brief Canonical phase representation. */
    std::uint32_t phase = 0;
    /** \brief Configured field-group name. */
    std::string name;
    /** \brief Continuous component count. */
    unsigned int component_count = 0;
    /** \brief Configured polynomial degree. */
    unsigned int degree = 0;

    /** \brief Serialize the exact support-field specification. */
    template<class Archive> void serialize(Archive& archive, const unsigned int /*version*/)
    {
        archive & phase;
        archive & name;
        archive & component_count;
        archive & degree;
    }
};

/** \brief Serialization-friendly geometry-field specification. */
struct GeometryFieldGroupWire {
    /** \brief Configured field-group name. */
    std::string name;
    /** \brief Exact component semantics. */
    GeometryFieldComponentsWire components;
    /** \brief Configured polynomial degree. */
    unsigned int degree = 0;

    /** \brief Serialize the exact geometry-field specification. */
    template<class Archive> void serialize(Archive& archive, const unsigned int /*version*/)
    {
        archive & name;
        archive & components;
        archive & degree;
    }
};

/** \brief Serialization-friendly discrete-metadata specification. */
struct DiscreteGeometryMetadataWire {
    /** \brief Configured metadata name. */
    std::string name;
    /** \brief Symbolic geometry-field target. */
    std::string geometry_field_group;
    /** \brief Target component index. */
    unsigned int component = 0;
    /** \brief Logical metadata-kind representation. */
    std::uint8_t kind = 0;

    /** \brief Serialize the exact metadata specification. */
    template<class Archive> void serialize(Archive& archive, const unsigned int /*version*/)
    {
        archive & name;
        archive & geometry_field_group;
        archive & component;
        archive & kind;
    }
};

/** \brief Exact canonical schema exchanged from rank zero. */
struct SpaceSpecificationWire {
    /** \brief Canonically ordered support-field specifications. */
    std::vector<PhaseSupportFieldGroupWire> phase_support_fields;
    /** \brief Canonically ordered geometry-field specifications. */
    std::vector<GeometryFieldGroupWire> geometry_fields;
    /** \brief Canonically ordered metadata specifications. */
    std::vector<DiscreteGeometryMetadataWire> discrete_metadata;

    /** \brief Serialize the exact canonical schema. */
    template<class Archive> void serialize(Archive& archive, const unsigned int /*version*/)
    {
        archive & phase_support_fields;
        archive & geometry_fields;
        archive & discrete_metadata;
    }
};

/** \brief Exact rank-zero-reference agreement record. */
struct SpaceDraftAgreementPacket {
    /** \brief Requested volume-mesh dimension. */
    std::uint64_t dimension = 0;
    /** \brief Whether the supplied support aggregate is active. */
    bool has_phase_supports = false;
    /** \brief Context-local support-set identity. */
    std::uint64_t phase_support_set_id = 0;
    /** \brief Context-local mesh-snapshot identity. */
    std::uint64_t mesh_snapshot_id = 0;
    /** \brief Epoch expected by the calling rank. */
    std::uint64_t expected_epoch = 0;
    /** \brief Exact canonical schema. */
    SpaceSpecificationWire specification;

    /** \brief Serialize the exact rank-agreement packet. */
    template<class Archive> void serialize(Archive& archive, const unsigned int /*version*/)
    {
        archive & dimension;
        archive & has_phase_supports;
        archive & phase_support_set_id;
        archive & mesh_snapshot_id;
        archive & expected_epoch;
        archive & specification;
    }
};

/** \brief Serialization-friendly form of one typed public error. */
struct SpaceDraftErrorWire {
    /** \brief Error-code representation. */
    std::uint8_t code = 0;
    /** \brief Offending world rank. */
    unsigned int rank = 0;
    /** \brief Active alternative in the public subject variant. */
    std::uint8_t subject_kind = 0;
    /** \brief Canonical subject position. */
    std::uint64_t sorted_index = 0;
    /** \brief Support-field subject storage. */
    PhaseSupportFieldGroupWire phase_support_field;
    /** \brief Geometry-field subject storage. */
    GeometryFieldGroupWire geometry_field;
    /** \brief Metadata subject storage. */
    DiscreteGeometryMetadataWire discrete_metadata;
    /** \brief Whether a phase subsubject exists. */
    bool has_phase = false;
    /** \brief Optional phase subsubject representation. */
    std::uint32_t phase = 0;
    /** \brief Whether a component subsubject exists. */
    bool has_component = false;
    /** \brief Optional component subsubject. */
    unsigned int component = 0;
    /** \brief Human-readable diagnostic. */
    std::string message;

    /** \brief Serialize one complete failure-only diagnostic. */
    template<class Archive> void serialize(Archive& archive, const unsigned int /*version*/)
    {
        archive & code;
        archive & rank;
        archive & subject_kind;
        archive & sorted_index;
        archive & phase_support_field;
        archive & geometry_field;
        archive & discrete_metadata;
        archive & has_phase;
        archive & phase;
        archive & has_component;
        archive & component;
        archive & message;
    }
};

/** \brief Canonical descriptor vectors produced after validation. */
struct CanonicalSpaceSchemaStorage {
    /** \brief Canonical support-field descriptors. */
    std::vector<PhaseSupportFieldGroupDescriptor> phase_support_fields;
    /** \brief Canonical continuous geometry-field descriptors. */
    std::vector<GeometryFieldGroupDescriptor> geometry_fields;
    /** \brief Canonical resolved metadata descriptors. */
    std::vector<DiscreteGeometryMetadataDescriptor> discrete_metadata;
};

/** \brief Test whether a configured name contains well-formed UTF-8. */
[[nodiscard]] bool is_valid_utf8(const std::string_view value) noexcept
{
    // NOLINTNEXTLINE(misc-include-cleaner): declared through simdutf's public umbrella header.
    return simdutf::validate_utf8(value.data(), value.size());
}

/** \brief Return the number of continuous components in either shape. */
[[nodiscard]] unsigned int component_count(const GeometryFieldComponents& components)
{
    if (const auto* unbound = std::get_if<UnboundFieldComponents>(&components)) {
        return unbound->count;
    }
    return static_cast<unsigned int>(std::get<PhaseBoundFieldComponents>(components).phases.size());
}

/** \brief Canonically sort all user-owned schema vectors and phase bindings. */
void sort_space_specification(SpaceSpecification& specification)
{
    std::ranges::sort(specification.phase_support_fields, [](const auto& left, const auto& right) {
        return std::tuple{left.phase.value(), std::string_view{left.name}} <
               std::tuple{right.phase.value(), std::string_view{right.name}};
    });
    for (auto& field : specification.geometry.continuous_fields) {
        if (auto* bound = std::get_if<PhaseBoundFieldComponents>(&field.components)) {
            std::ranges::sort(bound->phases, {}, &PhaseId::value);
        }
    }
    std::ranges::sort(specification.geometry.continuous_fields, {}, &GeometryFieldGroupSpecification::name);
    std::ranges::sort(specification.geometry.discrete_metadata, {}, &DiscreteGeometryMetadataSpecification::name);
}

/** \brief Convert public component semantics to their exact wire value. */
[[nodiscard]] GeometryFieldComponentsWire to_wire(const GeometryFieldComponents& components)
{
    if (const auto* unbound = std::get_if<UnboundFieldComponents>(&components)) {
        return {.kind = 0, .count = unbound->count, .phases = {}};
    }

    GeometryFieldComponentsWire wire{.kind = 1, .count = 0, .phases = {}};
    const auto& phases = std::get<PhaseBoundFieldComponents>(components).phases;
    wire.phases.reserve(phases.size());
    std::ranges::transform(phases, std::back_inserter(wire.phases), &PhaseId::value);
    return wire;
}

/** \brief Restore public component semantics from an internal wire value. */
[[nodiscard]] GeometryFieldComponents from_wire(const GeometryFieldComponentsWire& wire)
{
    if (wire.kind == 0) {
        return UnboundFieldComponents{.count = wire.count};
    }

    PhaseBoundFieldComponents bound;
    bound.phases.reserve(wire.phases.size());
    std::ranges::transform(wire.phases, std::back_inserter(bound.phases), &PhaseId::from_index);
    return bound;
}

/** \brief Convert one support-field specification for exact exchange. */
[[nodiscard]] PhaseSupportFieldGroupWire to_wire(const PhaseSupportFieldGroupSpecification& specification)
{
    return {.phase = specification.phase.value(),
            .name = specification.name,
            .component_count = specification.component_count,
            .degree = specification.degree};
}

/** \brief Restore one support-field specification from internal exchange. */
[[nodiscard]] PhaseSupportFieldGroupSpecification from_wire(const PhaseSupportFieldGroupWire& wire)
{
    return {.phase = PhaseId::from_index(wire.phase),
            .name = wire.name,
            .component_count = wire.component_count,
            .degree = wire.degree};
}

/** \brief Convert one geometry-field specification for exact exchange. */
[[nodiscard]] GeometryFieldGroupWire to_wire(const GeometryFieldGroupSpecification& specification)
{
    return {
        .name = specification.name, .components = to_wire(specification.components), .degree = specification.degree};
}

/** \brief Restore one geometry-field specification from internal exchange. */
[[nodiscard]] GeometryFieldGroupSpecification from_wire(const GeometryFieldGroupWire& wire)
{
    return {.name = wire.name, .components = from_wire(wire.components), .degree = wire.degree};
}

/** \brief Convert one metadata specification for exact exchange. */
[[nodiscard]] DiscreteGeometryMetadataWire to_wire(const DiscreteGeometryMetadataSpecification& specification)
{
    return {.name = specification.name,
            .geometry_field_group = specification.geometry_field_group,
            .component = specification.component,
            .kind = static_cast<std::uint8_t>(specification.kind)};
}

/** \brief Restore one metadata specification from internal exchange. */
[[nodiscard]] DiscreteGeometryMetadataSpecification from_wire(const DiscreteGeometryMetadataWire& wire)
{
    return {.name = wire.name,
            .geometry_field_group = wire.geometry_field_group,
            .component = wire.component,
            .kind = static_cast<DiscreteGeometryMetadataKind>(wire.kind)};
}

/** \brief Convert a complete canonical input schema for rank agreement. */
[[nodiscard]] SpaceSpecificationWire to_wire(const SpaceSpecification& specification)
{
    SpaceSpecificationWire wire;
    wire.phase_support_fields.reserve(specification.phase_support_fields.size());
    std::ranges::transform(specification.phase_support_fields, std::back_inserter(wire.phase_support_fields),
                           [](const auto& field) { return to_wire(field); });
    wire.geometry_fields.reserve(specification.geometry.continuous_fields.size());
    std::ranges::transform(specification.geometry.continuous_fields, std::back_inserter(wire.geometry_fields),
                           [](const auto& field) { return to_wire(field); });
    wire.discrete_metadata.reserve(specification.geometry.discrete_metadata.size());
    std::ranges::transform(specification.geometry.discrete_metadata, std::back_inserter(wire.discrete_metadata),
                           [](const auto& metadata) { return to_wire(metadata); });
    return wire;
}

/** \brief Construct a typed support-field diagnostic subject. */
[[nodiscard]] SpaceDraftErrorSubject support_subject(const std::size_t index,
                                                     const PhaseSupportFieldGroupSpecification& specification)
{
    return PhaseSupportFieldGroupErrorSubject{.sorted_index = index, .specification = specification};
}

/** \brief Construct a typed geometry-field diagnostic subject. */
[[nodiscard]] SpaceDraftErrorSubject geometry_subject(const std::size_t index,
                                                      const GeometryFieldGroupSpecification& specification,
                                                      const std::optional<PhaseId> phase = std::nullopt,
                                                      const std::optional<unsigned int> component = std::nullopt)
{
    return GeometryFieldGroupErrorSubject{
        .sorted_index = index, .specification = specification, .phase = phase, .component = component};
}

/** \brief Construct a typed metadata diagnostic subject. */
[[nodiscard]] SpaceDraftErrorSubject metadata_subject(const std::size_t index,
                                                      const DiscreteGeometryMetadataSpecification& specification)
{
    return DiscreteGeometryMetadataErrorSubject{.sorted_index = index, .specification = specification};
}

/** \brief Append one structured public error. */
void add_error(SpaceDraftErrors& errors, const SpaceDraftErrorCode code, const unsigned int rank,
               SpaceDraftErrorSubject subject, std::string message)
{
    errors.push_back({.code = code, .rank = rank, .subject = std::move(subject), .message = std::move(message)});
}

/** \brief Extract a deterministic position from any typed subject. */
[[nodiscard]] std::size_t subject_index(const SpaceDraftErrorSubject& subject) noexcept
{
    if (const auto* value = std::get_if<PhaseSupportFieldGroupErrorSubject>(&subject)) {
        return value->sorted_index;
    }
    if (const auto* value = std::get_if<GeometryFieldGroupErrorSubject>(&subject)) {
        return value->sorted_index;
    }
    if (const auto* value = std::get_if<DiscreteGeometryMetadataErrorSubject>(&subject)) {
        return value->sorted_index;
    }
    return 0;
}

/** \brief Extract an optional phase subsubject used for deterministic ordering. */
[[nodiscard]] std::optional<PhaseId> subject_phase(const SpaceDraftErrorSubject& subject) noexcept
{
    if (const auto* geometry = std::get_if<GeometryFieldGroupErrorSubject>(&subject)) {
        return geometry->phase;
    }
    return std::nullopt;
}

/** \brief Extract an optional component subsubject used for deterministic ordering. */
[[nodiscard]] std::optional<unsigned int> subject_component(const SpaceDraftErrorSubject& subject) noexcept
{
    if (const auto* geometry = std::get_if<GeometryFieldGroupErrorSubject>(&subject)) {
        return geometry->component;
    }
    return std::nullopt;
}

/** \brief Order errors by rank, rule, subject category, and canonical subject. */
[[nodiscard]] bool error_less(const SpaceDraftError& left, const SpaceDraftError& right) noexcept
{
    const auto left_phase = subject_phase(left.subject);
    const auto right_phase = subject_phase(right.subject);
    const auto left_component = subject_component(left.subject);
    const auto right_component = subject_component(right.subject);
    return std::tuple{left.rank,
                      static_cast<std::uint8_t>(left.code),
                      left.subject.index(),
                      subject_index(left.subject),
                      left_phase.has_value(),
                      left_phase.has_value() ? left_phase->value() : std::uint32_t{0},
                      left_component.has_value(),
                      left_component.value_or(0)} <
           std::tuple{right.rank,
                      static_cast<std::uint8_t>(right.code),
                      right.subject.index(),
                      subject_index(right.subject),
                      right_phase.has_value(),
                      right_phase.has_value() ? right_phase->value() : std::uint32_t{0},
                      right_component.has_value(),
                      right_component.value_or(0)};
}

/** \brief Sort errors into deterministic public order. */
void sort_errors(SpaceDraftErrors& errors) { std::ranges::sort(errors, error_less); }

/** \brief Validate one configured name while retaining its typed subject. */
void validate_name(const std::string_view name, const unsigned int rank, const SpaceDraftErrorSubject& subject,
                   const std::string_view category, SpaceDraftErrors& errors)
{
    if (name.empty()) {
        add_error(errors, SpaceDraftErrorCode::empty_name, rank, subject,
                  std::format("rank {} supplied an empty {} name", rank, category));
    }
    if (!is_valid_utf8(name)) {
        add_error(errors, SpaceDraftErrorCode::invalid_name_encoding, rank, subject,
                  std::format("rank {} supplied a {} name that is not valid UTF-8", rank, category));
    }
}

/** \brief Validate support-field references, names, and element shapes. */
void validate_phase_support_fields(const PhaseGraph& phase_graph, const SpaceSpecification& specification,
                                   const unsigned int rank, SpaceDraftErrors& errors)
{
    const auto& fields = specification.phase_support_fields;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const auto& field = fields.at(index);
        const auto subject = support_subject(index, field);
        validate_name(field.name, rank, subject, "phase-support field-group", errors);
        if (field.phase.value() >= phase_graph.phases().size()) {
            add_error(errors, SpaceDraftErrorCode::unknown_phase, rank, subject,
                      std::format("rank {} phase-support field '{}' references unknown phase ID {}", rank, field.name,
                                  field.phase.value()));
        }
        if (field.component_count == 0) {
            add_error(errors, SpaceDraftErrorCode::zero_component_count, rank, subject,
                      std::format("rank {} phase-support field '{}' has component count 0; expected a positive count",
                                  rank, field.name));
        }
        if (field.degree == 0) {
            add_error(errors, SpaceDraftErrorCode::invalid_polynomial_degree, rank, subject,
                      std::format("rank {} phase-support field '{}' has FE_Q degree 0; expected a positive degree",
                                  rank, field.name));
        }
        if (index > 0 && field.phase == fields.at(index - 1).phase && field.name == fields.at(index - 1).name) {
            add_error(errors, SpaceDraftErrorCode::duplicate_name, rank, subject,
                      std::format("rank {} repeats phase-support field name '{}' for phase ID {}", rank, field.name,
                                  field.phase.value()));
        }
    }
}

/** \brief Validate one phase-bound geometry field against the canonical graph. */
void validate_phase_bound_geometry_field(const PhaseGraph& phase_graph, const GeometryFieldGroupSpecification& field,
                                         const std::size_t index, const unsigned int rank, SpaceDraftErrors& errors)
{
    const auto& phases = std::get<PhaseBoundFieldComponents>(field.components).phases;
    const auto subject = geometry_subject(index, field);
    if (phases.empty()) {
        add_error(errors, SpaceDraftErrorCode::zero_component_count, rank, subject,
                  std::format("rank {} phase-bound geometry field '{}' has no components", rank, field.name));
    }

    const auto phase_count = phase_graph.phases().size();
    std::vector<std::size_t> counts(phase_count, 0);
    for (std::size_t component = 0; component < phases.size(); ++component) {
        const auto phase = phases.at(component);
        if (phase.value() >= phase_count) {
            add_error(errors, SpaceDraftErrorCode::unknown_phase_binding, rank,
                      geometry_subject(index, field, phase, static_cast<unsigned int>(component)),
                      std::format("rank {} geometry field '{}' component {} binds unknown phase ID {}", rank,
                                  field.name, component, phase.value()));
            continue;
        }
        auto& count = counts.at(phase.value());
        ++count;
        if (count > 1) {
            add_error(errors, SpaceDraftErrorCode::duplicate_phase_binding, rank,
                      geometry_subject(index, field, phase, static_cast<unsigned int>(component)),
                      std::format("rank {} geometry field '{}' binds phase ID {} more than once", rank, field.name,
                                  phase.value()));
        }
    }
    for (std::size_t phase = 0; phase < phase_count; ++phase) {
        if (counts.at(phase) == 0) {
            const auto phase_id = PhaseId::from_index(static_cast<PhaseId::representation_type>(phase));
            add_error(errors, SpaceDraftErrorCode::missing_phase_binding, rank,
                      geometry_subject(index, field, phase_id),
                      std::format("rank {} geometry field '{}' has no component bound to phase {} ({})", rank,
                                  field.name, phase, phase_graph.phase(phase_id).name));
        }
    }
}

/** \brief Validate geometry names, degrees, and all-or-nothing phase bindings. */
void validate_geometry_fields(const PhaseGraph& phase_graph, const SpaceSpecification& specification,
                              const unsigned int rank, SpaceDraftErrors& errors)
{
    const auto& fields = specification.geometry.continuous_fields;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const auto& field = fields.at(index);
        const auto subject = geometry_subject(index, field);
        validate_name(field.name, rank, subject, "geometry field-group", errors);
        if (field.degree == 0) {
            add_error(errors, SpaceDraftErrorCode::invalid_polynomial_degree, rank, subject,
                      std::format("rank {} geometry field '{}' has FE_Q degree 0; expected a positive degree", rank,
                                  field.name));
        }
        if (index > 0 && field.name == fields.at(index - 1).name) {
            add_error(errors, SpaceDraftErrorCode::duplicate_name, rank, subject,
                      std::format("rank {} repeats geometry field name '{}'", rank, field.name));
        }

        if (const auto* unbound = std::get_if<UnboundFieldComponents>(&field.components)) {
            if (unbound->count == 0) {
                add_error(errors, SpaceDraftErrorCode::zero_component_count, rank, subject,
                          std::format("rank {} geometry field '{}' has component count 0; expected a positive count",
                                      rank, field.name));
            }
            continue;
        }

        validate_phase_bound_geometry_field(phase_graph, field, index, rank, errors);
    }
}

/** \brief Return the uniquely named geometry field or no unambiguous target. */
[[nodiscard]] const GeometryFieldGroupSpecification*
find_unique_geometry_field(const std::vector<GeometryFieldGroupSpecification>& fields, const std::string_view name)
{
    const auto first = std::ranges::lower_bound(fields, name, {}, &GeometryFieldGroupSpecification::name);
    if (first == fields.end() || first->name != name) {
        return nullptr;
    }
    const auto next = std::next(first);
    if (next != fields.end() && next->name == name) {
        return nullptr;
    }
    return &*first;
}

/** \brief Validate metadata names, targets, component indices, and kinds. */
void validate_discrete_metadata(const SpaceSpecification& specification, const unsigned int rank,
                                SpaceDraftErrors& errors)
{
    const auto& fields = specification.geometry.continuous_fields;
    const auto& metadata = specification.geometry.discrete_metadata;
    std::vector<std::tuple<std::string_view, unsigned int, std::size_t>> targets;
    targets.reserve(metadata.size());

    for (std::size_t index = 0; index < metadata.size(); ++index) {
        const auto& item = metadata.at(index);
        const auto subject = metadata_subject(index, item);
        validate_name(item.name, rank, subject, "discrete geometry-metadata", errors);
        if (index > 0 && item.name == metadata.at(index - 1).name) {
            add_error(errors, SpaceDraftErrorCode::duplicate_name, rank, subject,
                      std::format("rank {} repeats discrete geometry-metadata name '{}'", rank, item.name));
        }
        if (item.kind != DiscreteGeometryMetadataKind::phase_label) {
            add_error(errors, SpaceDraftErrorCode::unsupported_metadata_kind, rank, subject,
                      std::format("rank {} metadata '{}' uses unsupported kind {}", rank, item.name,
                                  static_cast<unsigned int>(item.kind)));
        }

        const auto* field = find_unique_geometry_field(fields, item.geometry_field_group);
        if (field == nullptr) {
            add_error(errors, SpaceDraftErrorCode::unknown_metadata_field_group, rank, subject,
                      std::format("rank {} metadata '{}' cannot resolve one geometry field named '{}'", rank, item.name,
                                  item.geometry_field_group));
            continue;
        }

        const auto field_component_count = component_count(field->components);
        if (item.component >= field_component_count) {
            add_error(errors, SpaceDraftErrorCode::metadata_component_out_of_range, rank, subject,
                      std::format("rank {} metadata '{}' selects component {} of geometry field '{}', which has {} "
                                  "components",
                                  rank, item.name, item.component, item.geometry_field_group, field_component_count));
        }
        if (std::holds_alternative<PhaseBoundFieldComponents>(field->components)) {
            add_error(errors, SpaceDraftErrorCode::metadata_requires_unbound_components, rank, subject,
                      std::format("rank {} phase-label metadata '{}' targets phase-bound geometry field '{}'; "
                                  "expected an unbound field",
                                  rank, item.name, item.geometry_field_group));
        }
        if (item.component < field_component_count) {
            targets.emplace_back(item.geometry_field_group, item.component, index);
        }
    }

    std::ranges::sort(targets);
    for (std::size_t index = 1; index < targets.size(); ++index) {
        if (std::get<0>(targets.at(index)) == std::get<0>(targets.at(index - 1)) &&
            std::get<1>(targets.at(index)) == std::get<1>(targets.at(index - 1))) {
            const auto metadata_index = std::get<2>(targets.at(index));
            const auto& item = metadata.at(metadata_index);
            add_error(errors, SpaceDraftErrorCode::duplicate_metadata_target, rank,
                      metadata_subject(metadata_index, item),
                      std::format("rank {} metadata '{}' repeats target '{}[{}]'", rank, item.name,
                                  item.geometry_field_group, item.component));
        }
    }
}

/** \brief Validate every local semantic rule after canonical sorting. */
[[nodiscard]] SpaceDraftErrors validate_space_specification(const PhaseGraph& phase_graph,
                                                            const bool phase_supports_active,
                                                            const SpaceSpecification& specification,
                                                            const unsigned int rank)
{
    SpaceDraftErrors errors;
    if (!phase_supports_active) {
        add_error(errors, SpaceDraftErrorCode::inactive_phase_support_set, rank, {},
                  std::format("rank {} supplied a moved-from or otherwise inactive phase-support set", rank));
    }
    validate_phase_support_fields(phase_graph, specification, rank, errors);
    validate_geometry_fields(phase_graph, specification, rank, errors);
    validate_discrete_metadata(specification, rank, errors);
    sort_errors(errors);
    return errors;
}

/** \brief Describe one rank's exact canonical schema and provenance. */
template<int dim>
[[nodiscard]] SpaceDraftAgreementPacket make_agreement_packet(const PhaseSupportSet<dim>& phase_supports,
                                                              const std::uint64_t expected_epoch,
                                                              const SpaceSpecification& specification)
{
    const auto active = phase_supports.active();
    return {.dimension = dim,
            .has_phase_supports = active,
            .phase_support_set_id = active ? phase_supports.id().value() : std::uint64_t{0},
            .mesh_snapshot_id = active ? phase_supports.mesh_snapshot().id().value() : std::uint64_t{0},
            .expected_epoch = expected_epoch,
            .specification = to_wire(specification)};
}

/** \brief Add exact rank-zero-reference mismatch diagnostics. */
void add_agreement_errors(const SpaceDraftAgreementPacket& local, const SpaceDraftAgreementPacket& reference,
                          const unsigned int rank, SpaceDraftErrors& errors)
{
    if (local.dimension != reference.dimension) {
        add_error(errors, SpaceDraftErrorCode::dimension_mismatch, rank, {},
                  std::format("rank {} requested space dimension {}, but rank 0 requested {}", rank, local.dimension,
                              reference.dimension));
    }
    if (local.has_phase_supports && reference.has_phase_supports &&
        local.phase_support_set_id != reference.phase_support_set_id) {
        add_error(errors, SpaceDraftErrorCode::phase_support_set_mismatch, rank, {},
                  std::format("rank {} supplied phase-support set {}, but rank 0 supplied {}", rank,
                              local.phase_support_set_id, reference.phase_support_set_id));
    }
    if (local.has_phase_supports && reference.has_phase_supports &&
        local.mesh_snapshot_id != reference.mesh_snapshot_id) {
        add_error(errors, SpaceDraftErrorCode::mesh_snapshot_mismatch, rank, {},
                  std::format("rank {} supplied mesh snapshot {}, but rank 0 supplied {}", rank, local.mesh_snapshot_id,
                              reference.mesh_snapshot_id));
    }
    // Same-order collective calls advance every rank after the same successful
    // calls and advance none after failures. This defensive diagnostic can be
    // reached only after an earlier collective-contract violation; the reviewer
    // approved this narrow unreachable-code exclusion on 2026-09-04.
    if (local.expected_epoch != reference.expected_epoch) { // GCOVR_EXCL_BR_LINE
        // GCOVR_EXCL_START
        add_error(errors, SpaceDraftErrorCode::space_epoch_mismatch, rank, {},
                  std::format("rank {} expected space epoch {}, but rank 0 expected {}", rank, local.expected_epoch,
                              reference.expected_epoch));
    }
    // GCOVR_EXCL_STOP
    if (dealii::Utilities::pack(local.specification, false) !=
        dealii::Utilities::pack(reference.specification, false)) {
        add_error(errors, SpaceDraftErrorCode::schema_mismatch, rank, {},
                  std::format("rank {} canonical field schema differs from rank 0", rank));
    }
}

/** \brief Convert one typed public error to its failure-only wire form. */
[[nodiscard]] SpaceDraftErrorWire to_wire(SpaceDraftError error)
{
    SpaceDraftErrorWire wire{.code = static_cast<std::uint8_t>(error.code),
                             .rank = error.rank,
                             .subject_kind = static_cast<std::uint8_t>(error.subject.index()),
                             .sorted_index = 0,
                             .phase_support_field = {},
                             .geometry_field = {},
                             .discrete_metadata = {},
                             .has_phase = false,
                             .phase = 0,
                             .has_component = false,
                             .component = 0,
                             .message = std::move(error.message)};
    if (const auto* subject = std::get_if<PhaseSupportFieldGroupErrorSubject>(&error.subject)) {
        wire.sorted_index = subject->sorted_index;
        wire.phase_support_field = to_wire(subject->specification);
    }
    else if (const auto* subject = std::get_if<GeometryFieldGroupErrorSubject>(&error.subject)) {
        wire.sorted_index = subject->sorted_index;
        wire.geometry_field = to_wire(subject->specification);
        wire.has_phase = subject->phase.has_value();
        wire.phase = subject->phase.has_value() ? subject->phase->value() : std::uint32_t{0};
        wire.has_component = subject->component.has_value();
        wire.component = subject->component.value_or(0);
    }
    else if (const auto* subject = std::get_if<DiscreteGeometryMetadataErrorSubject>(&error.subject)) {
        wire.sorted_index = subject->sorted_index;
        wire.discrete_metadata = to_wire(subject->specification);
    }
    return wire;
}

/** \brief Restore one typed public error from its failure-only wire form. */
[[nodiscard]] SpaceDraftError from_wire(SpaceDraftErrorWire wire)
{
    SpaceDraftErrorSubject subject;
    if (wire.subject_kind == 1) {
        subject = PhaseSupportFieldGroupErrorSubject{.sorted_index = wire.sorted_index,
                                                     .specification = from_wire(wire.phase_support_field)};
    }
    else if (wire.subject_kind == 2) {
        subject = GeometryFieldGroupErrorSubject{
            .sorted_index = wire.sorted_index,
            .specification = from_wire(wire.geometry_field),
            .phase = wire.has_phase ? std::optional{PhaseId::from_index(wire.phase)} : std::nullopt,
            .component = wire.has_component ? std::optional{wire.component} : std::nullopt};
    }
    else if (wire.subject_kind == 3) {
        subject = DiscreteGeometryMetadataErrorSubject{.sorted_index = wire.sorted_index,
                                                       .specification = from_wire(wire.discrete_metadata)};
    }
    return {.code = static_cast<SpaceDraftErrorCode>(wire.code),
            .rank = wire.rank,
            .subject = std::move(subject),
            .message = std::move(wire.message)};
}

/** \brief Gather complete typed diagnostics only after collective failure. */
[[nodiscard]] SpaceDraftErrors collect_failure_errors(const MPI_Comm communicator, SpaceDraftErrors local_errors)
{
    std::vector<SpaceDraftErrorWire> local_wire;
    local_wire.reserve(local_errors.size());
    std::ranges::transform(local_errors, std::back_inserter(local_wire),
                           [](auto& error) { return to_wire(std::move(error)); });
    const auto gathered = dealii::Utilities::MPI::all_gather(communicator, local_wire);

    SpaceDraftErrors errors;
    for (const auto& rank_errors : gathered) {
        std::ranges::transform(rank_errors, std::back_inserter(errors),
                               [](auto error) { return from_wire(std::move(error)); });
    }
    sort_errors(errors);
    return errors;
}

/** \brief Build canonical descriptors and resolve metadata targets. */
[[nodiscard]] CanonicalSpaceSchemaStorage make_canonical_schema(SpaceSpecification specification)
{
    CanonicalSpaceSchemaStorage storage;
    storage.phase_support_fields.reserve(specification.phase_support_fields.size());
    for (std::size_t index = 0; index < specification.phase_support_fields.size(); ++index) {
        auto& field = specification.phase_support_fields.at(index);
        storage.phase_support_fields.push_back({.id = PhaseSupportFieldGroupId::from_index(
                                                    static_cast<PhaseSupportFieldGroupId::representation_type>(index)),
                                                .phase = field.phase,
                                                .name = std::move(field.name),
                                                .component_count = field.component_count,
                                                .degree = field.degree});
    }

    storage.geometry_fields.reserve(specification.geometry.continuous_fields.size());
    for (std::size_t index = 0; index < specification.geometry.continuous_fields.size(); ++index) {
        auto& field = specification.geometry.continuous_fields.at(index);
        storage.geometry_fields.push_back(
            {.id = GeometryFieldGroupId::from_index(static_cast<GeometryFieldGroupId::representation_type>(index)),
             .name = std::move(field.name),
             .components = std::move(field.components),
             .degree = field.degree});
    }

    storage.discrete_metadata.reserve(specification.geometry.discrete_metadata.size());
    for (std::size_t index = 0; index < specification.geometry.discrete_metadata.size(); ++index) {
        auto& item = specification.geometry.discrete_metadata.at(index);
        const auto field =
            std::ranges::lower_bound(storage.geometry_fields, std::string_view{item.geometry_field_group}, {},
                                     [](const auto& value) { return std::string_view{value.name}; });
        storage.discrete_metadata.push_back({.id = DiscreteGeometryMetadataId::from_index(
                                                 static_cast<DiscreteGeometryMetadataId::representation_type>(index)),
                                             .name = std::move(item.name),
                                             .geometry_field_group = field->id,
                                             .component = item.component,
                                             .kind = item.kind});
    }
    return storage;
}

} // namespace

SpaceSchema::SpaceSchema(std::vector<PhaseSupportFieldGroupDescriptor> phase_support_fields,
                         std::vector<GeometryFieldGroupDescriptor> geometry_fields,
                         std::vector<DiscreteGeometryMetadataDescriptor> discrete_geometry_metadata) noexcept :
    phase_support_fields_(std::move(phase_support_fields)),
    geometry_fields_(std::move(geometry_fields)),
    discrete_geometry_metadata_(std::move(discrete_geometry_metadata))
{
}

std::span<const PhaseSupportFieldGroupDescriptor> SpaceSchema::phase_support_fields() const noexcept
{
    return phase_support_fields_;
}

std::span<const GeometryFieldGroupDescriptor> SpaceSchema::geometry_fields() const noexcept { return geometry_fields_; }

std::span<const DiscreteGeometryMetadataDescriptor> SpaceSchema::discrete_geometry_metadata() const noexcept
{
    return discrete_geometry_metadata_;
}

template<int dim>
    requires(dim == 2 || dim == 3)
SpaceDraft<dim>::SpaceDraft(const SpaceEpoch epoch, PhaseSupportSet<dim> phase_supports, SpaceSchema schema) noexcept :
    epoch_(epoch), phase_supports_(std::move(phase_supports)), schema_(std::move(schema))
{
}

template<int dim>
    requires(dim == 2 || dim == 3)
SpaceDraft<dim>::~SpaceDraft() = default;

template<int dim>
    requires(dim == 2 || dim == 3)
SpaceDraft<dim>::SpaceDraft(SpaceDraft&& other) noexcept :
    epoch_(other.epoch_),
    phase_supports_(std::move(other.phase_supports_)),
    schema_(std::move(other.schema_)),
    field_spaces_(std::move(other.field_spaces_)),
    active_(std::exchange(other.active_, false))
{
}

template<int dim>
    requires(dim == 2 || dim == 3)
SpaceDraft<dim>& SpaceDraft<dim>::operator=(SpaceDraft&& other) noexcept
{
    if (this != &other) {
        // DoF handlers must be destroyed before the mesh snapshot they observe.
        field_spaces_.reset();
        epoch_ = other.epoch_;
        phase_supports_ = std::move(other.phase_supports_);
        schema_ = std::move(other.schema_);
        field_spaces_ = std::move(other.field_spaces_);
        active_ = std::exchange(other.active_, false);
    }
    return *this;
}

template<int dim>
    requires(dim == 2 || dim == 3)
std::span<const PhaseSupportFieldGroupSpace<dim>> SpaceDraft<dim>::phase_support_field_spaces() const noexcept
{
    return field_spaces_ == nullptr
               ? std::span<const PhaseSupportFieldGroupSpace<dim>>{}
               : std::span<const PhaseSupportFieldGroupSpace<dim>>{field_spaces_->phase_support_fields};
}

template<int dim>
    requires(dim == 2 || dim == 3)
std::span<const GeometryFieldGroupSpace<dim>> SpaceDraft<dim>::geometry_field_spaces() const noexcept
{
    return field_spaces_ == nullptr ? std::span<const GeometryFieldGroupSpace<dim>>{}
                                    : std::span<const GeometryFieldGroupSpace<dim>>{field_spaces_->geometry_fields};
}

template<int dim>
    requires(dim == 2 || dim == 3)
const PhaseSupportFieldGroupSpace<dim>&
SpaceDraft<dim>::phase_support_field_space(const PhaseSupportFieldGroupId id) const
{
    if (field_spaces_ == nullptr) {
        throw std::logic_error("phase-support field spaces have not been built");
    }
    if (id.value() >= field_spaces_->phase_support_fields.size()) {
        throw std::out_of_range(std::format("phase-support field-group ID {} is outside [0, {})", id.value(),
                                            field_spaces_->phase_support_fields.size()));
    }
    return field_spaces_->phase_support_fields.at(id.value());
}

template<int dim>
    requires(dim == 2 || dim == 3)
const GeometryFieldGroupSpace<dim>& SpaceDraft<dim>::geometry_field_space(const GeometryFieldGroupId id) const
{
    if (field_spaces_ == nullptr) {
        throw std::logic_error("geometry field spaces have not been built");
    }
    if (id.value() >= field_spaces_->geometry_fields.size()) {
        throw std::out_of_range(std::format("geometry field-group ID {} is outside [0, {})", id.value(),
                                            field_spaces_->geometry_fields.size()));
    }
    return field_spaces_->geometry_fields.at(id.value());
}

template<int dim>
    requires(dim == 2 || dim == 3)
SpaceDraftResult<dim> RiftContext::create_space_draft(PhaseSupportSet<dim> phase_supports,
                                                      SpaceSpecification specification)
{
    sort_space_specification(specification);
    auto local_errors =
        validate_space_specification(*phase_graph_, phase_supports.active(), specification, this_mpi_process());
    const auto local_packet = make_agreement_packet(phase_supports, next_space_epoch_index_, specification);
    const auto reference_packet = dealii::Utilities::MPI::broadcast(mpi_communicator(), local_packet, 0);
    const auto local_packet_bytes = dealii::Utilities::pack(local_packet, false);
    const auto reference_packet_bytes = dealii::Utilities::pack(reference_packet, false);
    const std::array<unsigned int, 2> local_flags{
        {static_cast<unsigned int>(local_errors.empty()),
         static_cast<unsigned int>(local_packet_bytes == reference_packet_bytes)}};
    std::array<unsigned int, 2> global_flags{};
    dealii::Utilities::MPI::min(dealii::make_array_view(local_flags), mpi_communicator(),
                                dealii::make_array_view(global_flags));

    if (global_flags.at(0) == 0 || global_flags.at(1) == 0) {
        add_agreement_errors(local_packet, reference_packet, this_mpi_process(), local_errors);
        sort_errors(local_errors);
        return std::unexpected(collect_failure_errors(mpi_communicator(), std::move(local_errors)));
    }

    auto canonical = make_canonical_schema(std::move(specification));
    SpaceSchema schema(std::move(canonical.phase_support_fields), std::move(canonical.geometry_fields),
                       std::move(canonical.discrete_metadata));
    const auto epoch = SpaceEpoch::from_index(next_space_epoch_index_);
    ++next_space_epoch_index_;
    return SpaceDraft<dim>(epoch, std::move(phase_supports), std::move(schema));
}

template<int dim>
    requires(dim == 2 || dim == 3)
FieldSpaceBuildResult RiftContext::build_field_spaces(SpaceDraft<dim>& draft, const FieldSpaceBuildOptions options)
{
    auto preflight = preflight_field_space_build(mpi_communicator(), draft, options.numbering);
    if (!preflight.has_value()) {
        return std::unexpected(std::move(preflight.error()));
    }

    auto field_spaces = std::make_unique<detail::FieldSpaceStorage<dim>>();
    const auto& phase_supports = draft.phase_supports();
    const auto& triangulation = phase_supports.mesh_snapshot().triangulation();

    const auto support_descriptors = draft.canonical_schema().phase_support_fields();
    field_spaces->phase_support_fields.reserve(support_descriptors.size());
    for (const auto& descriptor : support_descriptors) {
        field_spaces->phase_support_fields.push_back(
            PhaseSupportFieldGroupSpace<dim>(descriptor, draft.epoch(), options.numbering, phase_supports.id(),
                                             phase_supports.support(descriptor.phase), triangulation));
    }

    const auto geometry_descriptors = draft.canonical_schema().geometry_fields();
    field_spaces->geometry_fields.reserve(geometry_descriptors.size());
    for (const auto& descriptor : geometry_descriptors) {
        field_spaces->geometry_fields.push_back(
            GeometryFieldGroupSpace<dim>(descriptor, draft.epoch(), options.numbering, triangulation));
    }

    draft.field_spaces_ = std::move(field_spaces);
    return {};
}

template class SpaceDraft<2>;
template class SpaceDraft<3>;

template SpaceDraftResult<2> RiftContext::create_space_draft<2>(PhaseSupportSet<2>, SpaceSpecification);
template SpaceDraftResult<3> RiftContext::create_space_draft<3>(PhaseSupportSet<3>, SpaceSpecification);
template FieldSpaceBuildResult RiftContext::build_field_spaces<2>(SpaceDraft<2>&, FieldSpaceBuildOptions);
template FieldSpaceBuildResult RiftContext::build_field_spaces<3>(SpaceDraft<3>&, FieldSpaceBuildOptions);

} // namespace rift
