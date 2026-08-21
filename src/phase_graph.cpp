#include <algorithm>
#include <map>
/**
 * \file
 * \brief Validation, canonicalization, lookup, and serialization implementation for `PhaseGraph`.
 */

#include <rift/phase_graph.hpp>
#include <stdexcept>

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
    constexpr char hex_digits[] = "0123456789abcdef";

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
                output.push_back(hex_digits[character >> 4]);
                output.push_back(hex_digits[character & 0x0f]);
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
        return PhaseGraph(std::move(phases), std::move(interfaces));
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
    const auto found = std::lower_bound(phases_.begin(), phases_.end(), name,
                                        [](const auto& phase, const auto candidate) { return phase.name < candidate; });
    if (found == phases_.end() || found->name != name)
        return std::nullopt;

    return found->id;
}

std::optional<InterfaceId> PhaseGraph::find_interface(const std::string_view name) const noexcept
{
    const auto found = std::lower_bound(
        interfaces_.begin(), interfaces_.end(), name,
        [](const auto& material_interface, const auto candidate) { return material_interface.name < candidate; });
    if (found == interfaces_.end() || found->name != name)
        return std::nullopt;

    return found->id;
}

std::optional<InterfaceId> PhaseGraph::find_interface(const PhaseId first, const PhaseId second) const noexcept
{
    for (const auto& material_interface : interfaces_)
        if (unordered_pair(material_interface.minus_phase, material_interface.plus_phase) ==
            unordered_pair(first, second))
            return material_interface.id;

    return std::nullopt;
}

std::string PhaseGraph::canonical_json() const
{
    std::string output;
    output.reserve(128 + 96 * phases_.size() + 128 * interfaces_.size());
    output += R"({"schema":"rift.phase_graph","version":1,"phases":[)";

    for (std::size_t index = 0; index < phases_.size(); ++index) {
        if (index != 0)
            output.push_back(',');

        const auto& phase = phases_[index];
        output += R"({"id":)" + std::to_string(phase.id.value()) + R"(,"name":)";
        append_json_string(output, phase.name);
        output += R"(,"physics":)";
        append_json_string(output, phase.physics_key.value());
        output.push_back('}');
    }

    output += R"(],"interfaces":[)";
    for (std::size_t index = 0; index < interfaces_.size(); ++index) {
        if (index != 0)
            output.push_back(',');

        const auto& material_interface = interfaces_[index];
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

PhaseGraphResult make_phase_graph(std::vector<PhaseSpecification> phase_specifications,
                                  std::vector<InterfaceSpecification> interface_specifications,
                                  InterfaceCompatibilityCheck compatibility_check)
{
    PhaseGraphErrors errors;

    if (phase_specifications.empty())
        add_error(errors, PhaseGraphErrorCode::no_phases, "a phase graph must contain at least one phase");

    std::map<std::string, std::vector<std::size_t>, std::less<>> phase_occurrences;
    for (std::size_t index = 0; index < phase_specifications.size(); ++index) {
        const auto& phase = phase_specifications[index];
        if (phase.name.empty())
            add_error(errors, PhaseGraphErrorCode::empty_phase_name,
                      "phase at configuration index " + std::to_string(index) + " has an empty name");
        else
            phase_occurrences[phase.name].push_back(index);

        if (phase.physics_key.value().empty())
            add_error(errors, PhaseGraphErrorCode::empty_physics_key,
                      "phase '" + phase.name + "' has an empty physics key");
    }

    for (const auto& [name, occurrences] : phase_occurrences)
        if (occurrences.size() > 1)
            add_error(errors, PhaseGraphErrorCode::duplicate_phase_name,
                      "phase name '" + name + "' is declared more than once");

    std::vector<PhaseDescriptor> phases;
    std::map<std::string, PhaseId, std::less<>> phase_ids;
    for (const auto& [name, occurrences] : phase_occurrences) {
        if (occurrences.size() != 1)
            continue;

        const auto& specification = phase_specifications[occurrences.front()];
        if (specification.physics_key.value().empty())
            continue;

        const auto id = PhaseId::from_index(static_cast<std::uint32_t>(phases.size()));
        phase_ids.emplace(name, id);
        phases.push_back({.id = id, .name = name, .physics_key = specification.physics_key});
    }

    std::map<std::string, std::vector<std::size_t>, std::less<>> interface_occurrences;
    for (std::size_t index = 0; index < interface_specifications.size(); ++index) {
        const auto& material_interface = interface_specifications[index];
        if (material_interface.name.empty())
            add_error(errors, PhaseGraphErrorCode::empty_interface_name,
                      "interface at configuration index " + std::to_string(index) + " has an empty name");
        else
            interface_occurrences[material_interface.name].push_back(index);

        if (material_interface.operator_key.value().empty())
            add_error(errors, PhaseGraphErrorCode::empty_interface_operator_key,
                      "interface '" + material_interface.name + "' has an empty operator key");
    }

    for (const auto& [name, occurrences] : interface_occurrences)
        if (occurrences.size() > 1)
            add_error(errors, PhaseGraphErrorCode::duplicate_interface_name,
                      "interface name '" + name + "' is declared more than once");

    std::vector<ResolvedInterface> resolved_interfaces;
    std::map<PhasePair, std::vector<std::size_t>> pair_occurrences;
    for (const auto& specification : interface_specifications) {
        bool valid = !specification.name.empty() && !specification.operator_key.value().empty();
        if (!specification.name.empty() && interface_occurrences.at(specification.name).size() != 1)
            valid = false;

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

        if (!valid)
            continue;

        const auto resolved_index = resolved_interfaces.size();
        resolved_interfaces.push_back(
            {.specification = &specification, .minus_phase = minus->second, .plus_phase = plus->second});
        pair_occurrences[unordered_pair(minus->second, plus->second)].push_back(resolved_index);
    }

    for (const auto& [pair, occurrences] : pair_occurrences) {
        static_cast<void>(pair);
        if (occurrences.size() < 2)
            continue;

        std::vector<std::string_view> names;
        names.reserve(occurrences.size());
        for (const auto occurrence : occurrences) {
            resolved_interfaces[occurrence].duplicate_pair = true;
            names.push_back(resolved_interfaces[occurrence].specification->name);
        }
        std::sort(names.begin(), names.end());

        std::string message = "interfaces ";
        for (std::size_t index = 0; index < names.size(); ++index) {
            if (index != 0)
                message += ", ";
            message += "'" + std::string(names[index]) + "'";
        }
        message += " declare the same unordered phase pair";
        add_error(errors, PhaseGraphErrorCode::duplicate_phase_pair, std::move(message));
    }

    if (!resolved_interfaces.empty() && !compatibility_check) {
        add_error(errors, PhaseGraphErrorCode::missing_compatibility_check,
                  "a compatibility check is required when the phase graph contains interfaces");
    }
    else if (compatibility_check) {
        for (const auto& resolved : resolved_interfaces) {
            if (resolved.duplicate_pair)
                continue;

            const auto& minus = phases.at(resolved.minus_phase.value());
            const auto& plus = phases.at(resolved.plus_phase.value());
            if (auto reason = compatibility_check(minus, plus, *resolved.specification))
                add_error(errors, PhaseGraphErrorCode::incompatible_interface,
                          "interface '" + resolved.specification->name + "' is incompatible with minus phase '" +
                              minus.name + "' and plus phase '" + plus.name + "': " + *reason);
        }
    }

    if (!errors.empty())
        return std::unexpected(std::move(errors));

    std::sort(resolved_interfaces.begin(), resolved_interfaces.end(),
              [](const auto& left, const auto& right) { return left.specification->name < right.specification->name; });

    std::vector<InterfaceDescriptor> interfaces;
    interfaces.reserve(resolved_interfaces.size());
    for (const auto& resolved : resolved_interfaces) {
        const auto id = InterfaceId::from_index(static_cast<std::uint32_t>(interfaces.size()));
        interfaces.push_back({.id = id,
                              .name = resolved.specification->name,
                              .minus_phase = resolved.minus_phase,
                              .plus_phase = resolved.plus_phase,
                              .operator_key = resolved.specification->operator_key});
    }

    return detail::PhaseGraphFactory::create(std::move(phases), std::move(interfaces));
}

} // namespace rift
