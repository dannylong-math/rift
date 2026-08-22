#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
/**
 * \file
 * \brief Validation, canonicalization, lookup, and serialization implementation for `PhaseGraph`.
 */

#include <optional>
#include <rift/phase_graph.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rift {

/**
 * \brief Keep graph-construction helpers private to this implementation unit.
 *
 * Maintainers place helpers here when they support only phase-graph
 * validation or serialization and should not become linkable library API.
 */
namespace {

/**
 * \brief Add one correctly escaped string value to canonical JSON output.
 *
 * UTF-8 bytes at or above `0x20` are preserved. JSON control characters are
 * emitted using their short escape or a lowercase hexadecimal `\\u00xx`
 * escape.
 *
 * \code{.cpp}
 * std::string output;
 * append_json_string(output, "gas\nphase");
 * // output is now "\"gas\\nphase\"".
 * \endcode
 *
 * Callers append surrounding object keys and punctuation separately. Keeping
 * all string escaping here makes `canonical_json()` deterministic.
 *
 * \param output destination string to extend.
 * \param value unescaped string contents.
 * \ingroup phase_graph
 */
void append_json_string(std::string& output, const std::string_view value)
{
    constexpr std::string_view hex_digits = "0123456789abcdef";

    output.push_back('"');
    for (const unsigned char character : value) {
        switch (character) {
        case '"':
            output += "\\\"";
            break;
        case '\\':
            output += "\\\\";
            break;
        case '\b':
            output += "\\b";
            break;
        case '\f':
            output += "\\f";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (character < 0x20) {
                output += "\\u00";
                output.push_back(hex_digits.at(character >> 4));
                output.push_back(hex_digits.at(character & 0x0f));
            }
            else {
                output.push_back(static_cast<char>(character));
            }
        }
    }
    output.push_back('"');
}

/**
 * \brief Accumulate a validation error without abandoning the remaining checks.
 *
 * \code{.cpp}
 * PhaseGraphErrors errors;
 * add_error(errors,
 *           PhaseGraphErrorCode::empty_phase_name,
 *           "phase at configuration index 0 has an empty name");
 * \endcode
 *
 * Construction intentionally collects independent errors so configuration
 * users can repair several problems in one iteration.
 *
 * \param errors destination collection.
 * \param code machine-readable error classification.
 * \param message human-readable diagnostic text.
 * \ingroup phase_graph
 */
void add_error(PhaseGraphErrors& errors, const PhaseGraphErrorCode code, std::string message)
{
    errors.push_back({.code = code, .message = std::move(message)});
}

/**
 * \brief Carry a resolved interface through duplicate-pair and compatibility validation.
 *
 * \par When to use
 * Maintainers create this value only after both incident names have resolved to
 * canonical phase IDs. Keep it until duplicate-pair and registry validation
 * finish, then convert it into an `InterfaceDescriptor`.
 *
 * \par Typical use
 * \code{.cpp}
 * InterfaceSpecification specification{
 *     "surface", "liquid", "gas", "finite-rate"};
 * ResolvedInterface resolved{
 *     .specification = &specification,
 *     .minus_phase = PhaseId::from_index(1),
 *     .plus_phase = PhaseId::from_index(0),
 * };
 *
 * const PhasePair pair =
 *     unordered_pair(resolved.minus_phase, resolved.plus_phase);
 * \endcode
 *
 * \par Important behavior
 * `specification` is non-owning and must point into the input vector for the
 * entire validation pass. `duplicate_pair` suppresses compatibility callbacks
 * for an edge that cannot be admitted to the initial simple graph.
 * \ingroup phase_graph
 */
struct ResolvedInterface {
    /**
     * \brief Non-owning pointer into the construction input vector.
     */
    const InterfaceSpecification* specification;

    /**
     * \brief Canonical identity resolved from the configured minus-phase name.
     */
    PhaseId minus_phase;

    /**
     * \brief Canonical identity resolved from the configured plus-phase name.
     */
    PhaseId plus_phase;

    /**
     * \brief Whether another interface declares the same unordered phase pair.
     */
    bool duplicate_pair = false;
};

/**
 * \brief Use an unordered phase pair as a deterministic map key.
 *
 * \code{.cpp}
 * const PhasePair pair{0, 2};
 * std::map<PhasePair, std::size_t> counts;
 * ++counts[pair];
 * \endcode
 *
 * Construct these keys with `unordered_pair()` so the lower identifier is
 * always first.
 * \ingroup phase_graph
 */
using PhasePair = std::pair<std::uint32_t, std::uint32_t>;

/**
 * \brief Normalize two incident phase IDs for duplicate-edge lookup.
 *
 * \code{.cpp}
 * const PhasePair forward = unordered_pair(
 *     PhaseId::from_index(0), PhaseId::from_index(2));
 * const PhasePair reverse = unordered_pair(
 *     PhaseId::from_index(2), PhaseId::from_index(0));
 * const bool same_pair = forward == reverse;
 * \endcode
 *
 * This normalization is only for adjacency lookup. It must never replace the
 * physical minus/plus orientation stored in `ResolvedInterface`.
 *
 * \param first identity of either incident phase.
 * \param second identity of the other incident phase.
 * \return ascending pair of integer representations.
 * \ingroup phase_graph
 */
PhasePair unordered_pair(const PhaseId first, const PhaseId second)
{
    return std::minmax(first.value(), second.value());
}

/**
 * \brief Retain the canonical phase descriptors and their name lookup during validation.
 *
 * \par When to use
 * `validate_phases()` returns this maintainer-only bundle so interface
 * validation can resolve names without rebuilding the canonical phase list.
 *
 * \par Typical use
 * \code{.cpp}
 * PhaseGraphErrors errors;
 * const std::vector<PhaseSpecification> specifications{{"gas", "compressible"}};
 * ValidatedPhases validated = validate_phases(specifications, errors);
 * const PhaseId gas = validated.ids.at("gas");
 * \endcode
 *
 * \par Important behavior
 * `descriptors` is ordered by phase name and each descriptor ID equals its
 * vector index. `ids` contains only unique phases with a non-empty physics key.
 */
struct ValidatedPhases {
    /** \brief Canonical descriptors whose indices equal their stable phase IDs. */
    std::vector<PhaseDescriptor> descriptors;
    /** \brief Stable phase identities indexed by validated unique names. */
    std::map<std::string, PhaseId, std::less<>> ids;
};

/** \brief Count configuration positions for each non-empty name. */
using NameOccurrences = std::map<std::string, std::vector<std::size_t>, std::less<>>;

/**
 * \brief Validate phase declarations and build their canonical lookup.
 *
 * Maintainers call this before resolving interfaces. Independent phase errors
 * are appended to `errors`, while only unambiguous valid phases enter the
 * returned lookup.
 *
 * \param specifications phase declarations in configuration order.
 * \param errors collection receiving every independently detectable phase error.
 * \return canonical valid descriptors and their name-to-ID lookup.
 */
ValidatedPhases validate_phases(const std::vector<PhaseSpecification>& specifications, PhaseGraphErrors& errors)
{
    if (specifications.empty()) {
        add_error(errors, PhaseGraphErrorCode::no_phases, "a phase graph must contain at least one phase");
    }

    NameOccurrences occurrences;
    for (std::size_t index = 0; index < specifications.size(); ++index) {
        const auto& phase = specifications.at(index);
        if (phase.name.empty()) {
            add_error(errors, PhaseGraphErrorCode::empty_phase_name,
                      "phase at configuration index " + std::to_string(index) + " has an empty name");
        }
        else {
            occurrences.try_emplace(phase.name).first->second.push_back(index);
        }

        if (phase.physics_key.value().empty()) {
            add_error(errors, PhaseGraphErrorCode::empty_physics_key,
                      "phase '" + phase.name + "' has an empty physics key");
        }
    }

    for (const auto& [name, positions] : occurrences) {
        if (positions.size() > 1) {
            add_error(errors, PhaseGraphErrorCode::duplicate_phase_name,
                      "phase name '" + name + "' is declared more than once");
        }
    }

    ValidatedPhases validated;
    for (const auto& [name, positions] : occurrences) {
        if (positions.size() != 1) {
            continue;
        }

        const auto& specification = specifications.at(positions.front());
        if (specification.physics_key.value().empty()) {
            continue;
        }

        const auto id = PhaseId::from_index(static_cast<std::uint32_t>(validated.descriptors.size()));
        validated.ids.emplace(name, id);
        validated.descriptors.push_back({.id = id, .name = name, .physics_key = specification.physics_key});
    }
    return validated;
}

/**
 * \brief Validate interface names and operator keys before resolving phase names.
 *
 * \param specifications interface declarations in configuration order.
 * \param errors collection receiving name and operator-key errors.
 * \return positions of every declaration grouped by non-empty interface name.
 */
NameOccurrences collect_interface_occurrences(const std::vector<InterfaceSpecification>& specifications,
                                              PhaseGraphErrors& errors)
{
    NameOccurrences occurrences;
    for (std::size_t index = 0; index < specifications.size(); ++index) {
        const auto& material_interface = specifications.at(index);
        if (material_interface.name.empty()) {
            add_error(errors, PhaseGraphErrorCode::empty_interface_name,
                      "interface at configuration index " + std::to_string(index) + " has an empty name");
        }
        else {
            occurrences.try_emplace(material_interface.name).first->second.push_back(index);
        }

        if (material_interface.operator_key.value().empty()) {
            add_error(errors, PhaseGraphErrorCode::empty_interface_operator_key,
                      "interface '" + material_interface.name + "' has an empty operator key");
        }
    }

    for (const auto& [name, positions] : occurrences) {
        if (positions.size() > 1) {
            add_error(errors, PhaseGraphErrorCode::duplicate_interface_name,
                      "interface name '" + name + "' is declared more than once");
        }
    }
    return occurrences;
}

/**
 * \brief Resolve structurally valid interface endpoints into stable phase IDs.
 *
 * The result retains pointers into `specifications`; callers must keep that
 * vector alive through duplicate-pair checking and descriptor construction.
 *
 * \param specifications interface declarations to resolve.
 * \param interface_occurrences validated interface-name occurrence counts.
 * \param phase_ids canonical phase-name lookup.
 * \param resolved destination for structurally valid resolved interfaces.
 * \param pair_occurrences destination grouping resolved entries by unordered endpoints.
 * \param errors collection receiving self-edge and missing-phase errors.
 */
void resolve_interfaces(const std::vector<InterfaceSpecification>& specifications,
                        const NameOccurrences& interface_occurrences,
                        const std::map<std::string, PhaseId, std::less<>>& phase_ids,
                        std::vector<ResolvedInterface>& resolved,
                        std::map<PhasePair, std::vector<std::size_t>>& pair_occurrences, PhaseGraphErrors& errors)
{
    for (const auto& specification : specifications) {
        bool valid = !specification.name.empty() && !specification.operator_key.value().empty();
        if (!specification.name.empty() && interface_occurrences.at(specification.name).size() != 1) {
            valid = false;
        }

        if (specification.minus_phase == specification.plus_phase) {
            add_error(errors, PhaseGraphErrorCode::self_interface,
                      "interface '" + specification.name + "' joins phase '" + specification.minus_phase +
                          "' to itself");
            valid = false;
        }

        const auto minus = phase_ids.find(specification.minus_phase);
        if (minus == phase_ids.end()) {
            add_error(errors, PhaseGraphErrorCode::missing_incident_phase,
                      "interface '" + specification.name + "' refers to missing or non-unique minus phase '" +
                          specification.minus_phase + "'");
            valid = false;
        }

        const auto plus = phase_ids.find(specification.plus_phase);
        if (plus == phase_ids.end()) {
            add_error(errors, PhaseGraphErrorCode::missing_incident_phase,
                      "interface '" + specification.name + "' refers to missing or non-unique plus phase '" +
                          specification.plus_phase + "'");
            valid = false;
        }

        if (!valid) {
            continue;
        }

        const auto resolved_index = resolved.size();
        resolved.push_back({.specification = &specification, .minus_phase = minus->second, .plus_phase = plus->second});
        pair_occurrences.try_emplace(unordered_pair(minus->second, plus->second))
            .first->second.push_back(resolved_index);
    }
}

/**
 * \brief Mark every resolved edge that violates the one-edge-per-pair invariant.
 *
 * \param resolved resolved interfaces to mark in place.
 * \param pair_occurrences resolved indices grouped by unordered endpoint pair.
 * \param errors collection receiving one diagnostic for each duplicated pair.
 */
void mark_duplicate_phase_pairs(std::vector<ResolvedInterface>& resolved,
                                const std::map<PhasePair, std::vector<std::size_t>>& pair_occurrences,
                                PhaseGraphErrors& errors)
{
    for (const auto& [pair, positions] : pair_occurrences) {
        static_cast<void>(pair);
        if (positions.size() < 2) {
            continue;
        }

        std::vector<std::string_view> names;
        names.reserve(positions.size());
        for (const auto position : positions) {
            resolved.at(position).duplicate_pair = true;
            names.push_back(resolved.at(position).specification->name);
        }
        std::ranges::sort(names);

        std::string message = "interfaces ";
        for (std::size_t index = 0; index < names.size(); ++index) {
            if (index != 0) {
                message += ", ";
            }
            message += "'" + std::string(names.at(index)) + "'";
        }
        message += " declare the same unordered phase pair";
        add_error(errors, PhaseGraphErrorCode::duplicate_phase_pair, std::move(message));
    }
}

/**
 * \brief Run the registry compatibility callback for every unique valid edge.
 *
 * \param resolved structurally resolved interfaces.
 * \param phases canonical descriptors indexed by `PhaseId`.
 * \param compatibility_check callback supplied by the configured registries.
 * \param errors collection receiving callback or missing-callback failures.
 */
void validate_compatibility(const std::vector<ResolvedInterface>& resolved, const std::vector<PhaseDescriptor>& phases,
                            const InterfaceCompatibilityCheck& compatibility_check, PhaseGraphErrors& errors)
{
    if (!resolved.empty() && !compatibility_check) {
        add_error(errors, PhaseGraphErrorCode::missing_compatibility_check,
                  "a compatibility check is required when the phase graph contains interfaces");
        return;
    }

    if (!compatibility_check) {
        return;
    }

    for (const auto& material_interface : resolved) {
        if (material_interface.duplicate_pair) {
            continue;
        }

        const auto& minus = phases.at(material_interface.minus_phase.value());
        const auto& plus = phases.at(material_interface.plus_phase.value());
        if (auto reason = compatibility_check(minus, plus, *material_interface.specification)) {
            add_error(errors, PhaseGraphErrorCode::incompatible_interface,
                      "interface '" + material_interface.specification->name + "' is incompatible with minus phase '" +
                          minus.name + "' and plus phase '" + plus.name + "': " + *reason);
        }
    }
}

/**
 * \brief Convert validated resolved interfaces into canonical public descriptors.
 *
 * \param resolved resolved interfaces to sort by name before assigning IDs.
 * \return interface descriptors whose indices equal their stable IDs.
 */
std::vector<InterfaceDescriptor> make_interface_descriptors(std::vector<ResolvedInterface>& resolved)
{
    std::ranges::sort(resolved, [](const auto& left, const auto& right) {
        return left.specification->name < right.specification->name;
    });

    std::vector<InterfaceDescriptor> interfaces;
    interfaces.reserve(resolved.size());
    for (const auto& material_interface : resolved) {
        const auto id = InterfaceId::from_index(static_cast<std::uint32_t>(interfaces.size()));
        interfaces.push_back({.id = id,
                              .name = material_interface.specification->name,
                              .minus_phase = material_interface.minus_phase,
                              .plus_phase = material_interface.plus_phase,
                              .operator_key = material_interface.specification->operator_key});
    }
    return interfaces;
}

} // namespace

namespace detail {

/**
 * \brief Complete validated graph construction without exposing the constructor publicly.
 *
 * \par When to use
 * Only `make_phase_graph()` should call this maintainer-facing type, after all
 * errors have been ruled out and both descriptor arrays have been sorted and
 * assigned contiguous IDs.
 *
 * \par Typical use
 * \code{.cpp}
 * std::vector<PhaseDescriptor> phases{{
 *     .id = PhaseId::from_index(0),
 *     .name = "gas",
 *     .physics_key = PhysicsKey{"compressible"},
 * }};
 * std::vector<InterfaceDescriptor> interfaces;
 *
 * PhaseGraph graph = PhaseGraphFactory::create(
 *     std::move(phases), std::move(interfaces));
 * \endcode
 *
 * \par Important behavior
 * The factory performs no validation. Its callers must guarantee that each
 * descriptor's ID equals its vector index and that every interface references
 * valid phase IDs.
 * \ingroup phase_graph
 */
struct PhaseGraphFactory {
    /**
     * \brief Move canonical descriptor storage into the final graph.
     *
     * Use this only at the successful end of `make_phase_graph()`:
     *
     * \code{.cpp}
     * return detail::PhaseGraphFactory::create(
     *     std::move(phases), std::move(interfaces));
     * \endcode
     *
     * No copies or additional validation are performed.
     *
     * \param phases phase descriptors indexed by `PhaseId`.
     * \param interfaces interface descriptors indexed by `InterfaceId`.
     * \return immutable runtime phase graph.
     */
    static PhaseGraph create(std::vector<PhaseDescriptor> phases, std::vector<InterfaceDescriptor> interfaces)
    {
        return {std::move(phases), std::move(interfaces)};
    } // GCOVR_EXCL_LINE -- Clang assigns only an unreachable vector-move cleanup block to this brace.
};

} // namespace detail

PhaseGraph::PhaseGraph(std::vector<PhaseDescriptor> phases, std::vector<InterfaceDescriptor> interfaces) :
    phases_(std::move(phases)), interfaces_(std::move(interfaces))
{
}

const PhaseDescriptor& PhaseGraph::phase(const PhaseId id) const { return phases_.at(id.value()); }

const InterfaceDescriptor& PhaseGraph::material_interface(const InterfaceId id) const
{
    return interfaces_.at(id.value());
}

std::optional<PhaseId> PhaseGraph::find_phase(const std::string_view name) const noexcept
{
    const auto found = std::ranges::lower_bound(phases_, name, std::less<>{}, &PhaseDescriptor::name);
    if (found == phases_.end() || found->name != name) {
        return std::nullopt;
    }

    return found->id;
}

std::optional<InterfaceId> PhaseGraph::find_interface(const std::string_view name) const noexcept
{
    const auto found = std::ranges::lower_bound(interfaces_, name, std::less<>{}, &InterfaceDescriptor::name);
    if (found == interfaces_.end() || found->name != name) {
        return std::nullopt;
    }

    return found->id;
}

std::optional<InterfaceId> PhaseGraph::find_interface(const PhaseId first, const PhaseId second) const noexcept
{
    for (const auto& material_interface : interfaces_) {
        if (unordered_pair(material_interface.minus_phase, material_interface.plus_phase) ==
            unordered_pair(first, second)) {
            return material_interface.id;
        }
    }

    return std::nullopt;
}

std::string PhaseGraph::canonical_json() const
{
    std::string output;
    output.reserve(128 + (96 * phases_.size()) + (128 * interfaces_.size()));
    output += R"({"schema":"rift.phase_graph","version":1,"phases":[)";

    for (std::size_t index = 0; index < phases_.size(); ++index) {
        if (index != 0) {
            output.push_back(',');
        }

        const auto& phase = phases_.at(index);
        output += R"({"id":)" + std::to_string(phase.id.value()) + R"(,"name":)";
        append_json_string(output, phase.name);
        output += R"(,"physics":)";
        append_json_string(output, phase.physics_key.value());
        output.push_back('}');
    }

    output += R"(],"interfaces":[)";
    for (std::size_t index = 0; index < interfaces_.size(); ++index) {
        if (index != 0) {
            output.push_back(',');
        }

        const auto& material_interface = interfaces_.at(index);
        output += R"({"id":)" + std::to_string(material_interface.id.value()) + R"(,"name":)";
        append_json_string(output, material_interface.name);
        output += R"(,"minus_phase":)" + std::to_string(material_interface.minus_phase.value());
        output += R"(,"plus_phase":)" + std::to_string(material_interface.plus_phase.value());
        output += R"(,"operator":)";
        append_json_string(output, material_interface.operator_key.value());
        output.push_back('}');
    }
    output += "]}";

    return output;
}

PhaseGraphResult make_phase_graph(const std::vector<PhaseSpecification>& phase_specifications,
                                  const std::vector<InterfaceSpecification>& interface_specifications,
                                  const InterfaceCompatibilityCheck& compatibility_check)
{
    PhaseGraphErrors errors;
    auto validated_phases = validate_phases(phase_specifications, errors);
    const auto interface_occurrences = collect_interface_occurrences(interface_specifications, errors);
    std::vector<ResolvedInterface> resolved_interfaces;
    std::map<PhasePair, std::vector<std::size_t>> pair_occurrences;
    resolve_interfaces(interface_specifications, interface_occurrences, validated_phases.ids, resolved_interfaces,
                       pair_occurrences, errors);
    mark_duplicate_phase_pairs(resolved_interfaces, pair_occurrences, errors);
    validate_compatibility(resolved_interfaces, validated_phases.descriptors, compatibility_check, errors);

    if (!errors.empty()) {
        return std::unexpected(std::move(errors));
    }

    auto interfaces = make_interface_descriptors(resolved_interfaces);
    return detail::PhaseGraphFactory::create(std::move(validated_phases.descriptors), std::move(interfaces));
}

} // namespace rift
