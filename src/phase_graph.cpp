/**
 * \file
 * \brief Private local validation and construction for Rift phase graphs.
 */

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deal.II/base/mpi.h>
#include <exception>
#include <expected>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <mpi.h>
#include <optional>
#include <ranges>
#include <rift/phase_graph.hpp>
#include <rift/rift_context.hpp>
#include <simdutf.h> // NOLINT(misc-include-cleaner): simdutf's public umbrella owns this declaration.
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace rift {

/** \brief Opaque canonical storage owned by a published phase graph. */
struct PhaseGraph::Storage {
    /** \brief Canonically ordered phases. */
    std::vector<PhaseDescriptor> phases;

    /** \brief Canonically ordered interfaces. */
    std::vector<InterfaceDescriptor> interfaces;
};

PhaseGraph::PhaseGraph(std::unique_ptr<const Storage> storage) : storage_(std::move(storage)) {}

PhaseGraph::~PhaseGraph() = default;

std::span<const PhaseDescriptor> PhaseGraph::phases() const noexcept
{
    return {storage_->phases.data(), storage_->phases.size()};
}

std::span<const InterfaceDescriptor> PhaseGraph::interfaces() const noexcept
{
    return {storage_->interfaces.data(), storage_->interfaces.size()};
}

const PhaseDescriptor& PhaseGraph::phase(const PhaseId id) const { return storage_->phases.at(id.value()); }

const InterfaceDescriptor& PhaseGraph::material_interface(const InterfaceId id) const
{
    return storage_->interfaces.at(id.value());
}

std::optional<PhaseId> PhaseGraph::find_phase(const std::string_view name) const noexcept
{
    const auto position = std::ranges::lower_bound(
        storage_->phases, name, {}, [](const PhaseDescriptor& phase) { return std::string_view{phase.name}; });
    if (position == storage_->phases.end() || std::string_view{position->name} != name) {
        return std::nullopt;
    }
    return position->id;
}

std::optional<InterfaceId> PhaseGraph::find_interface(const std::string_view name) const noexcept
{
    const auto position =
        std::ranges::lower_bound(storage_->interfaces, name, {},
                                 [](const InterfaceDescriptor& interface) { return std::string_view{interface.name}; });
    if (position == storage_->interfaces.end() || std::string_view{position->name} != name) {
        return std::nullopt;
    }
    return position->id;
}

std::optional<InterfaceId> PhaseGraph::find_interface(const PhaseId first, const PhaseId second) const noexcept
{
    if (first == second || first.value() >= storage_->phases.size() || second.value() >= storage_->phases.size()) {
        return std::nullopt;
    }

    const auto first_endpoint = std::min(first.value(), second.value());
    const auto second_endpoint = std::max(first.value(), second.value());
    const auto position =
        std::ranges::find_if(storage_->interfaces, [first_endpoint, second_endpoint](const auto& interface) {
            return std::min(interface.minus_phase.value(), interface.plus_phase.value()) == first_endpoint &&
                   std::max(interface.minus_phase.value(), interface.plus_phase.value()) == second_endpoint;
        });
    if (position == storage_->interfaces.end()) {
        return std::nullopt;
    }
    return position->id;
}

namespace {

/** \brief Test whether a string contains well-formed UTF-8. */
[[nodiscard]] bool is_valid_utf8(const std::string_view value) noexcept
{
    // NOLINTNEXTLINE(misc-include-cleaner): declared through simdutf's public umbrella header.
    return simdutf::validate_utf8(value.data(), value.size());
}

/** \brief Sort phase specifications before assigning their IDs. */
void sort_phases(std::vector<PhaseSpecification>& phases)
{
    std::ranges::sort(phases, [](const auto& left, const auto& right) {
        return std::tuple{std::string_view{left.name}, left.physics_key.value()} <
               std::tuple{std::string_view{right.name}, right.physics_key.value()};
    });
}

/** \brief Sort interface specifications without changing their orientation. */
void sort_interfaces(std::vector<InterfaceSpecification>& interfaces)
{
    std::ranges::sort(interfaces, [](const auto& left, const auto& right) {
        return std::tuple{std::string_view{left.name}, std::string_view{left.minus_phase},
                          std::string_view{left.plus_phase}, left.operator_key.value()} <
               std::tuple{std::string_view{right.name}, std::string_view{right.minus_phase},
                          std::string_view{right.plus_phase}, right.operator_key.value()};
    });
}

/** \brief Append one collected local configuration error. */
void add_error(PhaseGraphErrors& errors, const PhaseGraphErrorCode code, std::string message,
               std::optional<PhaseGraphErrorSubject> subject = std::nullopt)
{
    errors.push_back({.code = code, .message = std::move(message), .subject = std::move(subject)});
}

/** \brief Snapshot one sorted phase as diagnostic subject metadata. */
[[nodiscard]] PhaseGraphErrorSubject make_error_subject(const std::size_t index,
                                                        const PhaseSpecification& specification)
{
    return PhaseErrorSubject{.sorted_index = index, .specification = specification};
}

/** \brief Snapshot one sorted interface as diagnostic subject metadata. */
[[nodiscard]] PhaseGraphErrorSubject make_error_subject(const std::size_t index,
                                                        const InterfaceSpecification& specification)
{
    return InterfaceErrorSubject{.sorted_index = index, .specification = specification};
}

/** \brief Stable names used when formatting machine-readable error codes. */
constexpr std::array phase_graph_error_code_names{
    std::string_view{"no_phases"},
    std::string_view{"empty_phase_name"},
    std::string_view{"empty_physics_key"},
    std::string_view{"duplicate_phase_name"},
    std::string_view{"empty_interface_name"},
    std::string_view{"empty_interface_operator_key"},
    std::string_view{"duplicate_interface_name"},
    std::string_view{"missing_incident_phase"},
    std::string_view{"self_interface"},
    std::string_view{"duplicate_phase_pair"},
    std::string_view{"missing_compatibility_check"},
    std::string_view{"invalid_utf8"},
    std::string_view{"incompatible_interface"},
    std::string_view{"compatibility_test_exception"},
    std::string_view{"phase_graph_creation_already_attempted"},
    std::string_view{"collective_input_mismatch"},
    std::string_view{"collective_compatibility_mismatch"},
};

static_assert(phase_graph_error_code_names.size() ==
              static_cast<std::size_t>(PhaseGraphErrorCode::collective_compatibility_mismatch) + 1);

/** \brief Return the stable spelling of one machine-readable error code. */
[[nodiscard]] std::string_view phase_graph_error_code_name(const PhaseGraphErrorCode code) noexcept
{
    const auto index = static_cast<std::size_t>(code);
    if (index >= phase_graph_error_code_names.size()) {
        return "unknown_error";
    }
    return phase_graph_error_code_names.at(index);
}

/** \brief Quote configured text while making unsafe bytes visible. */
[[nodiscard]] std::string quote_configured_value(const std::string_view value)
{
    const auto valid_utf8 = is_valid_utf8(value);
    std::string output{"\""};
    output.reserve(value.size() + 2);

    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        switch (byte) {
        case '\b':
            output += R"(\b)";
            break;
        case '\f':
            output += R"(\f)";
            break;
        case '\n':
            output += R"(\n)";
            break;
        case '\r':
            output += R"(\r)";
            break;
        case '\t':
            output += R"(\t)";
            break;
        case '\\':
            output += R"(\\)";
            break;
        case '"':
            output += R"(\")";
            break;
        default:
            if (byte < 0x20 || byte == 0x7F || (!valid_utf8 && byte >= 0x80)) {
                output += std::format(R"(\x{:02X})", static_cast<unsigned int>(byte));
            }
            else {
                output.push_back(character);
            }
        }
    }
    output.push_back('"');
    return output;
}

/** \brief One row in a human-readable configured-versus-expected table. */
struct DiagnosticField {
    std::string_view name;
    std::string configured;
    std::string_view expected;
};

/** \brief Describe the configured fields of one phase subject. */
[[nodiscard]] std::vector<DiagnosticField> diagnostic_fields(const PhaseErrorSubject& subject)
{
    return {
        {.name = "Name",
         .configured = quote_configured_value(subject.specification.name),
         .expected = "non-empty unique UTF-8 string"},
        {.name = "Physics",
         .configured = quote_configured_value(subject.specification.physics_key.value()),
         .expected = "non-empty UTF-8 phase-physics key"},
    };
}

/** \brief Describe the configured fields of one interface subject. */
[[nodiscard]] std::vector<DiagnosticField> diagnostic_fields(const InterfaceErrorSubject& subject)
{
    return {
        {.name = "Name",
         .configured = quote_configured_value(subject.specification.name),
         .expected = "non-empty unique UTF-8 string"},
        {.name = "Minus phase",
         .configured = quote_configured_value(subject.specification.minus_phase),
         .expected = "name of one unique configured phase"},
        {.name = "Plus phase",
         .configured = quote_configured_value(subject.specification.plus_phase),
         .expected = "different unique configured phase name"},
        {.name = "Operator",
         .configured = quote_configured_value(subject.specification.operator_key.value()),
         .expected = "compatible non-empty UTF-8 interface-operator key"},
    };
}

/** \brief Make a human-recognizable heading for one phase subject. */
[[nodiscard]] std::string diagnostic_heading(const PhaseErrorSubject& subject)
{
    if (subject.specification.name.empty()) {
        return std::format("Phase with empty name (canonical position {})", subject.sorted_index);
    }
    return std::format("Phase {}", quote_configured_value(subject.specification.name));
}

/** \brief Make a human-recognizable heading for one interface subject. */
[[nodiscard]] std::string diagnostic_heading(const InterfaceErrorSubject& subject)
{
    if (subject.specification.name.empty()) {
        return std::format("Interface with empty name (canonical position {})", subject.sorted_index);
    }
    return std::format("Interface {}", quote_configured_value(subject.specification.name));
}

/** \brief Return whether two atomic errors identify the same sorted input. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the comparison is symmetric.
[[nodiscard]] bool same_error_subject(const PhaseGraphErrorSubject& left, const PhaseGraphErrorSubject& right) noexcept
{
    if (const auto* left_phase = std::get_if<PhaseErrorSubject>(&left)) {
        const auto* right_phase = std::get_if<PhaseErrorSubject>(&right);
        return right_phase != nullptr && left_phase->sorted_index == right_phase->sorted_index;
    }

    const auto* left_interface = std::get_if<InterfaceErrorSubject>(&left);
    const auto* right_interface = std::get_if<InterfaceErrorSubject>(&right);
    return right_interface != nullptr && left_interface->sorted_index == right_interface->sorted_index;
}

/** \brief Atomic errors collected beneath one optional configuration subject. */
struct DiagnosticGroup {
    const PhaseGraphErrorSubject* subject;
    std::vector<const PhaseGraphError*> errors;
};

/** \brief Group subject errors while preserving each group's first occurrence. */
[[nodiscard]] std::vector<DiagnosticGroup> group_errors(const std::span<const PhaseGraphError> errors)
{
    std::vector<DiagnosticGroup> groups;
    groups.reserve(errors.size());
    for (const auto& error : errors) {
        if (!error.subject.has_value()) {
            groups.push_back({.subject = nullptr, .errors = {&error}});
            continue;
        }

        const auto position = std::ranges::find_if(groups, [&error](const DiagnosticGroup& group) {
            return group.subject != nullptr && same_error_subject(*group.subject, *error.subject);
        });
        if (position == groups.end()) {
            groups.push_back({.subject = &*error.subject, .errors = {&error}});
        }
        else {
            position->errors.push_back(&error);
        }
    }
    return groups;
}

/** \brief Append one padded table cell. */
void append_padded(std::string& output, const std::string_view value, const std::size_t width)
{
    output += value;
    output.append(width - value.size(), ' ');
}

/** \brief Format one subject table and its grouped atomic errors. */
[[nodiscard]] std::string format_subject_errors(const DiagnosticGroup& group)
{
    auto heading = std::visit([](const auto& subject) { return diagnostic_heading(subject); }, *group.subject);
    auto fields = std::visit([](const auto& subject) { return diagnostic_fields(subject); }, *group.subject);

    std::size_t field_width = std::string_view{"Field"}.size();
    std::size_t configured_width = std::string_view{"Configured"}.size();
    std::size_t expected_width = std::string_view{"Expected"}.size();
    for (const auto& field : fields) {
        field_width = std::max(field_width, field.name.size());
        configured_width = std::max(configured_width, field.configured.size());
        expected_width = std::max(expected_width, field.expected.size());
    }

    std::string output = std::move(heading);
    output += "\n\n";
    append_padded(output, "Field", field_width);
    output += "  ";
    append_padded(output, "Configured", configured_width);
    output += "  Expected\n";
    output.append(field_width + configured_width + expected_width + 4, '-');
    for (const auto& field : fields) {
        output.push_back('\n');
        append_padded(output, field.name, field_width);
        output += "  ";
        append_padded(output, field.configured, configured_width);
        output += "  ";
        output += field.expected;
    }

    output += "\n\nErrors";
    for (const auto* error : group.errors) {
        output += std::format("\n  [{}] {}", phase_graph_error_code_name(error->code), error->message);
    }
    return output;
}

/** \brief Format one error that has no phase or interface subject. */
[[nodiscard]] std::string format_unscoped_error(const PhaseGraphError& error)
{
    return std::format("[{}] {}", phase_graph_error_code_name(error.code), error.message);
}

/** \brief Format all diagnostic groups without performing output. */
[[nodiscard]] std::string format_phase_graph_errors_impl(const std::span<const PhaseGraphError> errors)
{
    std::string output;
    for (const auto& group : group_errors(errors)) {
        if (!output.empty()) {
            output += "\n\n";
        }
        output +=
            group.subject == nullptr ? format_unscoped_error(*group.errors.front()) : format_subject_errors(group);
    }
    return output;
}

/** \brief Group sorted phase positions by a valid, non-empty name. */
using NameOccurrences = std::unordered_map<std::string, std::vector<std::size_t>>;

/** \brief Validate phase fields and record where each usable name occurs. */
[[nodiscard]] NameOccurrences validate_phases(const std::vector<PhaseSpecification>& phases, PhaseGraphErrors& errors)
{
    if (phases.empty()) {
        add_error(errors, PhaseGraphErrorCode::no_phases, "a phase graph must contain at least one phase");
    }

    NameOccurrences occurrences;
    occurrences.reserve(phases.size());
    for (std::size_t index = 0; index < phases.size(); ++index) {
        const auto& phase = phases.at(index);
        const auto subject = make_error_subject(index, phase);
        const auto name_is_utf8 = is_valid_utf8(phase.name);
        const auto key_is_utf8 = is_valid_utf8(phase.physics_key.value());

        if (phase.name.empty()) {
            add_error(errors, PhaseGraphErrorCode::empty_phase_name,
                      std::format("phase at sorted index {} has an empty name", index), subject);
        }
        if (phase.physics_key.value().empty()) {
            add_error(errors, PhaseGraphErrorCode::empty_physics_key,
                      std::format("phase at sorted index {} has an empty physics key", index), subject);
        }
        if (!name_is_utf8) {
            add_error(errors, PhaseGraphErrorCode::invalid_utf8,
                      std::format("phase at sorted index {} has invalid UTF-8 in its name", index), subject);
        }
        if (!key_is_utf8) {
            add_error(errors, PhaseGraphErrorCode::invalid_utf8,
                      std::format("phase at sorted index {} has invalid UTF-8 in its physics key", index), subject);
        }

        if (!phase.name.empty() && name_is_utf8) {
            auto& positions = occurrences[phase.name];
            positions.push_back(index);
            if (positions.size() == 2) {
                add_error(errors, PhaseGraphErrorCode::duplicate_phase_name,
                          std::format("phase name '{}' is declared more than once", phase.name));
            }
        }
    }
    return occurrences;
}

/** \brief Validate interface names and operator keys. */
void validate_interface_fields(const std::vector<InterfaceSpecification>& interfaces, PhaseGraphErrors& errors)
{
    std::unordered_map<std::string, std::size_t> name_counts;
    name_counts.reserve(interfaces.size());

    for (std::size_t index = 0; index < interfaces.size(); ++index) {
        const auto& interface = interfaces.at(index);
        const auto subject = make_error_subject(index, interface);
        const auto name_is_utf8 = is_valid_utf8(interface.name);
        const auto operator_is_utf8 = is_valid_utf8(interface.operator_key.value());

        if (interface.name.empty()) {
            add_error(errors, PhaseGraphErrorCode::empty_interface_name,
                      std::format("interface at sorted index {} has an empty name", index), subject);
        }
        if (interface.operator_key.value().empty()) {
            add_error(errors, PhaseGraphErrorCode::empty_interface_operator_key,
                      std::format("interface at sorted index {} has an empty operator key", index), subject);
        }
        if (!name_is_utf8) {
            add_error(errors, PhaseGraphErrorCode::invalid_utf8,
                      std::format("interface at sorted index {} has invalid UTF-8 in its name", index), subject);
        }
        if (!operator_is_utf8) {
            add_error(errors, PhaseGraphErrorCode::invalid_utf8,
                      std::format("interface at sorted index {} has invalid UTF-8 in its operator key", index),
                      subject);
        }

        if (!interface.name.empty() && name_is_utf8) {
            auto& count = name_counts[interface.name];
            ++count;
            if (count == 2) {
                add_error(errors, PhaseGraphErrorCode::duplicate_interface_name,
                          std::format("interface name '{}' is declared more than once", interface.name));
            }
        }
    }
}

/** \brief Sorted indices of the two ends of an unordered phase pair. */
using PhasePair = std::pair<std::size_t, std::size_t>;

/** \brief Interface positions grouped by unordered phase pair. */
using PhasePairOccurrences = std::map<PhasePair, std::vector<std::size_t>>;

/** \brief Return whether one phase name occurs exactly once. */
[[nodiscard]] bool is_unique_phase(const NameOccurrences& phases, const std::string& name)
{
    const auto position = phases.find(name);
    return position != phases.end() && position->second.size() == 1;
}

/** \brief Validate and resolve one interface's endpoint topology. */
void validate_interface_topology_entry(const InterfaceSpecification& interface, const std::size_t index,
                                       const NameOccurrences& phase_occurrences, PhasePairOccurrences& pair_occurrences,
                                       PhaseGraphErrors& errors)
{
    const auto subject = make_error_subject(index, interface);
    const auto minus_is_utf8 = is_valid_utf8(interface.minus_phase);
    const auto plus_is_utf8 = is_valid_utf8(interface.plus_phase);

    if (!minus_is_utf8) {
        add_error(errors, PhaseGraphErrorCode::invalid_utf8,
                  std::format("interface at sorted index {} has invalid UTF-8 in its minus-phase reference", index),
                  subject);
    }
    if (!plus_is_utf8) {
        add_error(errors, PhaseGraphErrorCode::invalid_utf8,
                  std::format("interface at sorted index {} has invalid UTF-8 in its plus-phase reference", index),
                  subject);
    }

    const auto minus_position = minus_is_utf8 ? phase_occurrences.find(interface.minus_phase) : phase_occurrences.end();
    const auto plus_position = plus_is_utf8 ? phase_occurrences.find(interface.plus_phase) : phase_occurrences.end();

    if (minus_is_utf8 && minus_position == phase_occurrences.end()) {
        add_error(errors, PhaseGraphErrorCode::missing_incident_phase,
                  std::format("interface at sorted index {} refers to missing minus phase '{}'", index,
                              interface.minus_phase),
                  subject);
    }
    if (plus_is_utf8 && plus_position == phase_occurrences.end()) {
        add_error(
            errors, PhaseGraphErrorCode::missing_incident_phase,
            std::format("interface at sorted index {} refers to missing plus phase '{}'", index, interface.plus_phase),
            subject);
    }

    const auto is_self_interface = minus_is_utf8 && plus_is_utf8 && interface.minus_phase == interface.plus_phase;
    if (is_self_interface) {
        add_error(errors, PhaseGraphErrorCode::self_interface,
                  std::format("interface at sorted index {} joins phase '{}' to itself", index, interface.minus_phase),
                  subject);
    }

    if (is_self_interface || !is_unique_phase(phase_occurrences, interface.minus_phase) ||
        !is_unique_phase(phase_occurrences, interface.plus_phase)) {
        return;
    }

    const auto minus_index = minus_position->second.front();
    const auto plus_index = plus_position->second.front();
    pair_occurrences[{std::min(minus_index, plus_index), std::max(minus_index, plus_index)}].push_back(index);
}

/** \brief Describe sorted interface positions without copying invalid UTF-8. */
[[nodiscard]] std::string describe_interfaces(const std::vector<InterfaceSpecification>& interfaces,
                                              const std::vector<std::size_t>& positions)
{
    std::string declarations;
    for (const auto position : positions) {
        if (!declarations.empty()) {
            declarations += ", ";
        }
        const auto& name = interfaces.at(position).name;
        declarations += name.empty() || !is_valid_utf8(name) ? std::format("#{}", position) : std::format("'{}'", name);
    }
    return declarations;
}

/** \brief Report every unordered phase pair declared by more than one interface. */
void report_duplicate_phase_pairs(const std::vector<PhaseSpecification>& phases,
                                  const std::vector<InterfaceSpecification>& interfaces,
                                  const PhasePairOccurrences& pair_occurrences, PhaseGraphErrors& errors)
{
    for (const auto& [pair, positions] : pair_occurrences) {
        if (positions.size() < 2) {
            continue;
        }

        add_error(errors, PhaseGraphErrorCode::duplicate_phase_pair,
                  std::format("interfaces {} declare the same unordered phase pair ('{}', '{}')",
                              describe_interfaces(interfaces, positions), phases.at(pair.first).name,
                              phases.at(pair.second).name));
    }
}

/** \brief Collect endpoint, self-edge, and duplicate-pair errors. */
void validate_interface_topology(const std::vector<PhaseSpecification>& phases,
                                 const std::vector<InterfaceSpecification>& interfaces,
                                 const NameOccurrences& phase_occurrences, PhaseGraphErrors& errors)
{
    PhasePairOccurrences pair_occurrences;
    for (std::size_t index = 0; index < interfaces.size(); ++index) {
        validate_interface_topology_entry(interfaces.at(index), index, phase_occurrences, pair_occurrences, errors);
    }
    report_duplicate_phase_pairs(phases, interfaces, pair_occurrences, errors);
}

/** \brief One sorted phase with an ID equal to its vector position. */
struct LocalPhase {
    /** \brief Contiguous identifier assigned after sorting. */
    PhaseId id;

    /** \brief Canonically ordered owning phase specification. */
    PhaseSpecification specification;
};

/** \brief One sorted interface retaining its declared physical orientation. */
struct LocalInterface {
    /** \brief Contiguous identifier assigned after sorting. */
    InterfaceId id;

    /** \brief Resolved identifier of the declared minus phase. */
    PhaseId minus_phase;

    /** \brief Resolved identifier of the declared plus phase. */
    PhaseId plus_phase;

    /** \brief Canonically ordered owning interface specification. */
    InterfaceSpecification specification;
};

/** \brief Private graph content consumed later by the collective factory. */
struct LocalPhaseGraphCandidate {
    /** \brief Canonically ordered phases. */
    std::vector<LocalPhase> phases;

    /** \brief Canonically ordered interfaces. */
    std::vector<LocalInterface> interfaces;
};

/** \brief A local canonical candidate or its collected structural errors. */
using LocalPhaseGraphResult = std::expected<LocalPhaseGraphCandidate, PhaseGraphErrors>;

/** \brief Exact private fields exchanged to compare local graph construction. */
using AgreementRecord = std::vector<std::string>;

/** \brief Pair a local graph result with the exact record used for rank agreement. */
struct LocalPhaseGraphAttempt {
    /** \brief Structured fields compared exactly across ranks. */
    AgreementRecord agreement_record;

    /** \brief Local candidate or collected structural errors. */
    LocalPhaseGraphResult result;
};

/** \brief Append an integer to the structured agreement record. */
template<class Integer> void append_integer(AgreementRecord& record, const Integer value)
{
    record.push_back(std::to_string(value));
}

/** \brief Record every sorted input field without delimiter-based encoding. */
[[nodiscard]] AgreementRecord make_input_agreement_record(const PhaseGraphSpecification& specification)
{
    AgreementRecord record{"rift.phase_graph.input.v1", "phases"};
    append_integer(record, specification.phases.size());
    for (const auto& phase : specification.phases) {
        record.push_back(phase.name);
        record.emplace_back(phase.physics_key.value());
    }

    record.emplace_back("interfaces");
    append_integer(record, specification.interfaces.size());
    for (const auto& interface : specification.interfaces) {
        record.push_back(interface.name);
        record.push_back(interface.minus_phase);
        record.push_back(interface.plus_phase);
        record.emplace_back(interface.operator_key.value());
    }
    return record;
}

/** \brief Append a failed local result to its agreement record. */
void append_error_result(AgreementRecord& record, const PhaseGraphErrors& errors)
{
    record.emplace_back("errors");
    append_integer(record, errors.size());
    for (const auto& error : errors) {
        append_integer(record, static_cast<unsigned int>(error.code));
        record.push_back(error.message);
    }
}

/** \brief Append the derived IDs and endpoints of a successful local candidate. */
void append_candidate_result(AgreementRecord& record, const LocalPhaseGraphCandidate& candidate)
{
    record.emplace_back("candidate");
    append_integer(record, candidate.phases.size());
    for (const auto& phase : candidate.phases) {
        append_integer(record, phase.id.value());
    }

    append_integer(record, candidate.interfaces.size());
    for (const auto& interface : candidate.interfaces) {
        append_integer(record, interface.id.value());
        append_integer(record, interface.minus_phase.value());
        append_integer(record, interface.plus_phase.value());
    }
}

/** \brief Construct a sorted local result and its exact agreement record. */
[[nodiscard]] LocalPhaseGraphAttempt build_local_phase_graph_attempt(PhaseGraphSpecification specification,
                                                                     const bool compatibility_test_available)
{
    sort_phases(specification.phases);
    sort_interfaces(specification.interfaces);
    auto agreement_record = make_input_agreement_record(specification);

    PhaseGraphErrors errors;
    const auto phase_occurrences = validate_phases(specification.phases, errors);
    validate_interface_fields(specification.interfaces, errors);
    validate_interface_topology(specification.phases, specification.interfaces, phase_occurrences, errors);

    if (!specification.interfaces.empty() && !compatibility_test_available) {
        add_error(errors, PhaseGraphErrorCode::missing_compatibility_check,
                  "a compatibility test is required when the phase graph contains interfaces");
    }

    if (!errors.empty()) {
        append_error_result(agreement_record, errors);
        return {.agreement_record = std::move(agreement_record), .result = std::unexpected(std::move(errors))};
    }

    LocalPhaseGraphCandidate candidate;
    candidate.phases.reserve(specification.phases.size());
    std::unordered_map<std::string, PhaseId> phase_ids;
    phase_ids.reserve(specification.phases.size());
    for (auto& phase : specification.phases) {
        const auto id = PhaseId::from_index(static_cast<std::uint32_t>(candidate.phases.size()));
        phase_ids.emplace(phase.name, id);
        candidate.phases.push_back({.id = id, .specification = std::move(phase)});
    }

    candidate.interfaces.reserve(specification.interfaces.size());
    for (auto& interface : specification.interfaces) {
        const auto id = InterfaceId::from_index(static_cast<std::uint32_t>(candidate.interfaces.size()));
        const auto minus_phase = phase_ids.at(interface.minus_phase);
        const auto plus_phase = phase_ids.at(interface.plus_phase);
        candidate.interfaces.push_back(
            {.id = id, .minus_phase = minus_phase, .plus_phase = plus_phase, .specification = std::move(interface)});
    }
    append_candidate_result(agreement_record, candidate);
    return {.agreement_record = std::move(agreement_record), .result = std::move(candidate)};
}

/** \brief Find the first rank whose exact agreement record differs from rank zero. */
[[nodiscard]] std::optional<unsigned int> find_first_mismatching_rank(const MPI_Comm communicator,
                                                                      const AgreementRecord& local_record)
{
    const auto records = dealii::Utilities::MPI::all_gather(communicator, local_record);
    for (std::size_t rank = 1; rank < records.size(); ++rank) {
        if (records.at(rank) != records.front()) {
            return static_cast<unsigned int>(rank);
        }
    }
    return std::nullopt;
}

/** \brief Return a local graph result only when every world rank constructed it identically. */
[[nodiscard]] LocalPhaseGraphResult build_collectively_agreed_local_phase_graph(const MPI_Comm communicator,
                                                                                PhaseGraphSpecification specification,
                                                                                const bool compatibility_test_available)
{
    auto attempt = build_local_phase_graph_attempt(std::move(specification), compatibility_test_available);
    const auto mismatching_rank = find_first_mismatching_rank(communicator, attempt.agreement_record);
    if (mismatching_rank.has_value()) {
        PhaseGraphErrors errors;
        add_error(errors, PhaseGraphErrorCode::collective_input_mismatch,
                  std::format("canonical phase-graph input or structural result on rank {} differs from rank 0",
                              mismatching_rank.value()));
        return std::unexpected(std::move(errors));
    }
    return std::move(attempt.result);
}

/** \brief Pair local compatibility errors with the exact record used for rank agreement. */
struct LocalCompatibilityAttempt {
    /** \brief Structured compatibility outcomes compared exactly across ranks. */
    AgreementRecord agreement_record;

    /** \brief Local rejection and caught-exception diagnostics. */
    PhaseGraphErrors errors;
};

/** \brief Format complete phase and interface context around one compatibility outcome. */
[[nodiscard]] std::string format_compatibility_error(const LocalPhase& minus_phase, const LocalPhase& plus_phase,
                                                     const LocalInterface& interface, const std::string_view outcome)
{
    return std::format(
        "interface '{}' (operator '{}') between minus phase '{}' (physics '{}') and plus phase '{}' (physics '{}') {}",
        interface.specification.name, interface.specification.operator_key.value(), minus_phase.specification.name,
        minus_phase.specification.physics_key.value(), plus_phase.specification.name,
        plus_phase.specification.physics_key.value(), outcome);
}

/** \brief Invoke every compatibility test locally and record its exact outcome. */
[[nodiscard]] LocalCompatibilityAttempt evaluate_interface_compatibility(const LocalPhaseGraphCandidate& candidate,
                                                                         InterfaceCompatibilityTest& compatibility_test)
{
    LocalCompatibilityAttempt attempt{.agreement_record = {"rift.phase_graph.compatibility.v1"}, .errors = {}};
    append_integer(attempt.agreement_record, candidate.interfaces.size());

    for (const auto& interface : candidate.interfaces) {
        const auto& minus_phase = candidate.phases.at(interface.minus_phase.value());
        const auto& plus_phase = candidate.phases.at(interface.plus_phase.value());
        const auto subject = make_error_subject(interface.id.value(), interface.specification);
        append_integer(attempt.agreement_record, interface.id.value());

        try {
            auto decision =
                compatibility_test(minus_phase.specification, plus_phase.specification, interface.specification);
            if (decision.has_value()) {
                attempt.agreement_record.emplace_back("accepted");
                continue;
            }

            attempt.agreement_record.emplace_back("rejected");
            attempt.agreement_record.push_back(decision.error());
            add_error(attempt.errors, PhaseGraphErrorCode::incompatible_interface,
                      format_compatibility_error(minus_phase, plus_phase, interface,
                                                 std::format("was rejected: {}", decision.error())),
                      subject);
        }
        catch (const std::exception& exception) {
            attempt.agreement_record.emplace_back("exception");
            attempt.agreement_record.emplace_back("std::exception");
            attempt.agreement_record.emplace_back(exception.what());
            add_error(attempt.errors, PhaseGraphErrorCode::compatibility_test_exception,
                      format_compatibility_error(minus_phase, plus_phase, interface,
                                                 std::format("compatibility test threw: {}", exception.what())),
                      subject);
        }
        catch (...) {
            attempt.agreement_record.emplace_back("exception");
            attempt.agreement_record.emplace_back("non-standard");
            add_error(attempt.errors, PhaseGraphErrorCode::compatibility_test_exception,
                      format_compatibility_error(minus_phase, plus_phase, interface,
                                                 "compatibility test threw a non-standard exception"),
                      subject);
        }
    }
    return attempt;
}

/** \brief Return a local candidate only when every rank reports identical compatibility outcomes. */
[[nodiscard]] LocalPhaseGraphResult
build_collectively_compatible_local_phase_graph(const MPI_Comm communicator, PhaseGraphSpecification specification,
                                                InterfaceCompatibilityTest compatibility_test)
{
    auto candidate_result = build_collectively_agreed_local_phase_graph(communicator, std::move(specification),
                                                                        static_cast<bool>(compatibility_test));
    if (!candidate_result.has_value()) {
        return std::unexpected(std::move(candidate_result.error()));
    }

    auto candidate = std::move(candidate_result).value();
    auto compatibility = evaluate_interface_compatibility(candidate, compatibility_test);
    const auto mismatching_rank = find_first_mismatching_rank(communicator, compatibility.agreement_record);
    if (mismatching_rank.has_value()) {
        PhaseGraphErrors errors;
        add_error(
            errors, PhaseGraphErrorCode::collective_compatibility_mismatch,
            std::format("interface compatibility outcomes on rank {} differ from rank 0", mismatching_rank.value()));
        return std::unexpected(std::move(errors));
    }
    if (!compatibility.errors.empty()) {
        return std::unexpected(std::move(compatibility.errors));
    }
    return candidate;
}

} // namespace

std::string format_phase_graph_errors(const std::span<const PhaseGraphError> errors)
{
    return format_phase_graph_errors_impl(errors);
}

PhaseGraphResult RiftContext::create_phase_graph(PhaseGraphSpecification specification,
                                                 InterfaceCompatibilityTest compatibility_test)
{
    const auto creation_was_already_attempted = phase_graph_creation_attempted_;
    phase_graph_creation_attempted_ = true;

    const AgreementRecord context_state{"rift.phase_graph.context.v1",
                                        creation_was_already_attempted ? "attempted" : "not-attempted"};
    const auto mismatching_rank = find_first_mismatching_rank(mpi_communicator(), context_state);

    // Reaching this branch requires ranks to have made different prior calls,
    // which cannot occur under the one-context, same-order collective contract.
    // The reviewer approved this narrow unreachable-code exclusion on 2026-09-02.
    if (mismatching_rank.has_value()) { // GCOVR_EXCL_BR_LINE
        // GCOVR_EXCL_START
        PhaseGraphErrors errors;
        add_error(errors, PhaseGraphErrorCode::collective_input_mismatch,
                  std::format("phase-graph creation state on rank {} differs from rank 0", mismatching_rank.value()));
        return std::unexpected(std::move(errors));
    }
    // GCOVR_EXCL_STOP
    if (creation_was_already_attempted) {
        PhaseGraphErrors errors;
        add_error(errors, PhaseGraphErrorCode::phase_graph_creation_already_attempted,
                  "this RiftContext has already consumed its single phase-graph creation attempt");
        return std::unexpected(std::move(errors));
    }

    auto candidate_result = build_collectively_compatible_local_phase_graph(
        mpi_communicator(), std::move(specification), std::move(compatibility_test));
    if (!candidate_result.has_value()) {
        return std::unexpected(std::move(candidate_result.error()));
    }

    auto candidate = std::move(candidate_result).value();
    auto storage = std::make_unique<PhaseGraph::Storage>();
    storage->phases.reserve(candidate.phases.size());
    for (auto& phase : candidate.phases) {
        storage->phases.push_back({.id = phase.id,
                                   .name = std::move(phase.specification.name),
                                   .physics_key = std::move(phase.specification.physics_key)});
    }
    storage->interfaces.reserve(candidate.interfaces.size());
    for (auto& interface : candidate.interfaces) {
        storage->interfaces.push_back({.id = interface.id,
                                       .name = std::move(interface.specification.name),
                                       .minus_phase = interface.minus_phase,
                                       .plus_phase = interface.plus_phase,
                                       .operator_key = std::move(interface.specification.operator_key)});
    }

    phase_graph_ = std::unique_ptr<const PhaseGraph>{new PhaseGraph(std::move(storage))};
    return std::cref(*phase_graph_);
}

} // namespace rift
