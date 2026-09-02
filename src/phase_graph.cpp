/**
 * \file
 * \brief Private local validation and construction for Rift phase graphs.
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <map>
#include <ranges>
#include <rift/phase_graph.hpp>
#include <simdutf.h> // NOLINT(misc-include-cleaner): simdutf's public umbrella owns this declaration.
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rift {
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
void add_error(PhaseGraphErrors& errors, const PhaseGraphErrorCode code, std::string message)
{
    errors.push_back({.code = code, .message = std::move(message)});
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
        const auto name_is_utf8 = is_valid_utf8(phase.name);
        const auto key_is_utf8 = is_valid_utf8(phase.physics_key.value());

        if (phase.name.empty()) {
            add_error(errors, PhaseGraphErrorCode::empty_phase_name,
                      std::format("phase at sorted index {} has an empty name", index));
        }
        if (phase.physics_key.value().empty()) {
            add_error(errors, PhaseGraphErrorCode::empty_physics_key,
                      std::format("phase at sorted index {} has an empty physics key", index));
        }
        if (!name_is_utf8) {
            add_error(errors, PhaseGraphErrorCode::invalid_utf8,
                      std::format("phase at sorted index {} has invalid UTF-8 in its name", index));
        }
        if (!key_is_utf8) {
            add_error(errors, PhaseGraphErrorCode::invalid_utf8,
                      std::format("phase at sorted index {} has invalid UTF-8 in its physics key", index));
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
        const auto name_is_utf8 = is_valid_utf8(interface.name);
        const auto operator_is_utf8 = is_valid_utf8(interface.operator_key.value());

        if (interface.name.empty()) {
            add_error(errors, PhaseGraphErrorCode::empty_interface_name,
                      std::format("interface at sorted index {} has an empty name", index));
        }
        if (interface.operator_key.value().empty()) {
            add_error(errors, PhaseGraphErrorCode::empty_interface_operator_key,
                      std::format("interface at sorted index {} has an empty operator key", index));
        }
        if (!name_is_utf8) {
            add_error(errors, PhaseGraphErrorCode::invalid_utf8,
                      std::format("interface at sorted index {} has invalid UTF-8 in its name", index));
        }
        if (!operator_is_utf8) {
            add_error(errors, PhaseGraphErrorCode::invalid_utf8,
                      std::format("interface at sorted index {} has invalid UTF-8 in its operator key", index));
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
    const auto minus_is_utf8 = is_valid_utf8(interface.minus_phase);
    const auto plus_is_utf8 = is_valid_utf8(interface.plus_phase);

    if (!minus_is_utf8) {
        add_error(errors, PhaseGraphErrorCode::invalid_utf8,
                  std::format("interface at sorted index {} has invalid UTF-8 in its minus-phase reference", index));
    }
    if (!plus_is_utf8) {
        add_error(errors, PhaseGraphErrorCode::invalid_utf8,
                  std::format("interface at sorted index {} has invalid UTF-8 in its plus-phase reference", index));
    }

    const auto minus_position = minus_is_utf8 ? phase_occurrences.find(interface.minus_phase) : phase_occurrences.end();
    const auto plus_position = plus_is_utf8 ? phase_occurrences.find(interface.plus_phase) : phase_occurrences.end();

    if (minus_is_utf8 && minus_position == phase_occurrences.end()) {
        add_error(errors, PhaseGraphErrorCode::missing_incident_phase,
                  std::format("interface at sorted index {} refers to missing minus phase '{}'", index,
                              interface.minus_phase));
    }
    if (plus_is_utf8 && plus_position == phase_occurrences.end()) {
        add_error(
            errors, PhaseGraphErrorCode::missing_incident_phase,
            std::format("interface at sorted index {} refers to missing plus phase '{}'", index, interface.plus_phase));
    }

    const auto is_self_interface = minus_is_utf8 && plus_is_utf8 && interface.minus_phase == interface.plus_phase;
    if (is_self_interface) {
        add_error(errors, PhaseGraphErrorCode::self_interface,
                  std::format("interface at sorted index {} joins phase '{}' to itself", index, interface.minus_phase));
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
    PhaseId id;
    PhaseSpecification specification;
};

/** \brief One sorted interface retaining its declared physical orientation. */
struct LocalInterface {
    InterfaceId id;
    PhaseId minus_phase;
    PhaseId plus_phase;
    InterfaceSpecification specification;
};

/** \brief Private graph content consumed later by the collective factory. */
struct LocalPhaseGraphCandidate {
    std::vector<LocalPhase> phases;
    std::vector<LocalInterface> interfaces;
};

using LocalPhaseGraphResult = std::expected<LocalPhaseGraphCandidate, PhaseGraphErrors>;

/** \brief Construct a sorted local candidate or every deterministic structural error. */
[[maybe_unused, nodiscard]] LocalPhaseGraphResult build_local_phase_graph(PhaseGraphSpecification specification,
                                                                          const bool compatibility_test_available)
{
    sort_phases(specification.phases);
    sort_interfaces(specification.interfaces);

    PhaseGraphErrors errors;
    const auto phase_occurrences = validate_phases(specification.phases, errors);
    validate_interface_fields(specification.interfaces, errors);
    validate_interface_topology(specification.phases, specification.interfaces, phase_occurrences, errors);

    if (!specification.interfaces.empty() && !compatibility_test_available) {
        add_error(errors, PhaseGraphErrorCode::missing_compatibility_check,
                  "a compatibility test is required when the phase graph contains interfaces");
    }

    if (!errors.empty()) {
        return std::unexpected(std::move(errors));
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
    return candidate;
}

} // namespace
} // namespace rift
