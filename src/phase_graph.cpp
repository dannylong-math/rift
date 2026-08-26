#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <functional>
#include <limits>
#include <map>
#include <memory>
/**
 * \file
 * \brief Validation, canonicalization, lookup, and serialization implementation for `PhaseGraph`.
 */

#include "phase_graph_internal.hpp"
#include "run_configuration_internal.hpp"

#include <mpi.h>
#if __has_include(<mpi_proto.h>)
#include <mpi_proto.h>
#endif
#include <new>
#include <optional>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
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
 * \brief Carry a user's original callback exception across the internal allocation boundary.
 *
 * \code{.cpp}
 * const auto compatibility_callback = [] { throw std::runtime_error{"registry failure"}; };
 * std::exception_ptr captured;
 * try {
 *   compatibility_callback();
 * } catch (...) {
 *   captured = std::current_exception();
 * }
 * if (captured) {
 *   try {
 *     throw CompatibilityCallbackException{captured};
 *   } catch (const CompatibilityCallbackException &failure) {
 *     std::rethrow_exception(failure.original);
 *   }
 * }
 * \endcode
 *
 * Internal `std::bad_alloc` is fatal after graph-input agreement, but a
 * callback-thrown `std::bad_alloc` remains a user exception and must be
 * rethrown unchanged. This private carrier distinguishes those origins.
 */
struct CompatibilityCallbackException {
    /** \brief Original exception captured at the callback boundary. */
    std::exception_ptr original;
};

/**
 * \brief Invoke a run's fatal MPI handler and make non-return explicit.
 * \param abort retained MPI-compatible fatal operation.
 * \param communicator communicator whose collective cannot safely continue.
 * \param status MPI status or representability failure code.
 */
[[noreturn]] void invoke_mpi_abort(const detail::MpiAbort abort, const MPI_Comm communicator, const int status)
{
    abort(communicator, status);
    std::unreachable();
}

/**
 * \brief Compare UTF-8 spellings by their unsigned bytes.
 * \param left first exact spelling.
 * \param right second exact spelling.
 * \return true when `left` precedes `right` bytewise.
 */
bool bytewise_less(const std::string_view left, const std::string_view right) noexcept
{
    return std::ranges::lexicographical_compare(
        left, right, {}, [](const char value) { return static_cast<unsigned char>(value); },
        [](const char value) { return static_cast<unsigned char>(value); });
}

/**
 * \brief Transparent bytewise ordering for owned strings and borrowed views.
 */
struct BytewiseLess {
    /** \brief Enable heterogeneous map lookup. */
    using is_transparent = void;

    /**
     * \brief Order two exact spellings without locale or Unicode normalization.
     * \param left first spelling.
     * \param right second spelling.
     * \return bytewise ordering result.
     */
    bool operator()(const std::string_view left, const std::string_view right) const noexcept
    {
        return bytewise_less(left, right);
    }
};

/**
 * \brief Test whether one byte lies in a closed unsigned-byte range.
 * \param value byte to test.
 * \param lower inclusive lower bound.
 * \param upper inclusive upper bound.
 * \return true when `lower <= value <= upper`.
 */
bool in_byte_range(const unsigned char value, const unsigned char lower, const unsigned char upper) noexcept
{
    return value >= lower && value <= upper;
}

/** \brief Read one byte without signed-char ordering effects. */
unsigned char utf8_byte(const std::string_view value, const std::size_t index) noexcept
{
    return static_cast<unsigned char>(value.at(index));
}

/** \brief Test the RFC 3629 continuation-byte range. */
bool is_utf8_continuation(const unsigned char value) noexcept { return in_byte_range(value, 0x80U, 0xbfU); }

/** \brief Validate the constrained second byte of a three-byte sequence. */
bool valid_three_byte_second(const unsigned char first, const unsigned char second) noexcept
{
    if (first == 0xe0U) {
        return in_byte_range(second, 0xa0U, 0xbfU);
    }
    if (first == 0xedU) {
        return in_byte_range(second, 0x80U, 0x9fU);
    }
    return is_utf8_continuation(second);
}

/** \brief Validate the constrained second byte of a four-byte sequence. */
bool valid_four_byte_second(const unsigned char first, const unsigned char second) noexcept
{
    if (first == 0xf0U) {
        return in_byte_range(second, 0x90U, 0xbfU);
    }
    if (first == 0xf4U) {
        return in_byte_range(second, 0x80U, 0x8fU);
    }
    return is_utf8_continuation(second);
}

/** \brief Return one valid scalar's encoded width, or zero for malformed input. */
std::size_t valid_utf8_sequence_size(const std::string_view value, const std::size_t index) noexcept
{
    const auto remaining = value.size() - index;
    const auto first = utf8_byte(value, index);
    if (first <= 0x7fU) {
        return 1;
    }
    if (in_byte_range(first, 0xc2U, 0xdfU)) {
        return remaining >= 2 && is_utf8_continuation(utf8_byte(value, index + 1)) ? 2 : 0;
    }
    if (in_byte_range(first, 0xe0U, 0xefU)) {
        return remaining >= 3 && valid_three_byte_second(first, utf8_byte(value, index + 1)) &&
                       is_utf8_continuation(utf8_byte(value, index + 2))
                   ? 3
                   : 0;
    }
    if (in_byte_range(first, 0xf0U, 0xf4U)) {
        return remaining >= 4 && valid_four_byte_second(first, utf8_byte(value, index + 1)) &&
                       is_utf8_continuation(utf8_byte(value, index + 2)) &&
                       is_utf8_continuation(utf8_byte(value, index + 3))
                   ? 4
                   : 0;
    }
    return 0;
}

/**
 * \brief Validate one exact byte spelling against RFC 3629 UTF-8.
 * \param value bytes to validate without normalization.
 * \return true when the complete value encodes only Unicode scalar values.
 */
bool is_valid_utf8(const std::string_view value) noexcept
{
    std::size_t index = 0;
    while (index < value.size()) {
        const auto sequence_size = valid_utf8_sequence_size(value, index);
        if (sequence_size == 0) {
            return false;
        }
        index += sequence_size;
    }
    return true;
}

/**
 * \brief Append one unsigned integer in a platform-independent fixed-width encoding.
 * \param output byte string to extend.
 * \param value integer to append most-significant byte first.
 */
void append_uint64(std::string& output, const std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
}

/**
 * \brief Append a string with an unambiguous byte length.
 * \param output byte string to extend.
 * \param value exact bytes to append.
 */
void append_length_prefixed(std::string& output, const std::string_view value)
{
    append_uint64(output, value.size());
    output.append(value);
}

/**
 * \brief Sort replicated graph input into its declaration-order-independent form.
 * \param phases phase declarations to copy and sort by every logical field.
 * \param interfaces interface declarations to copy and sort by every logical field.
 * \return canonical phase and interface declaration vectors.
 */
std::pair<std::vector<PhaseSpecification>, std::vector<InterfaceSpecification>>
canonicalize_input(const std::vector<PhaseSpecification>& phases, const std::vector<InterfaceSpecification>& interfaces)
{
    auto canonical_phases = phases;
    std::ranges::sort(canonical_phases, [](const auto& left, const auto& right) {
        if (left.name != right.name) {
            return bytewise_less(left.name, right.name);
        }
        return bytewise_less(left.physics_key.value(), right.physics_key.value());
    });

    auto canonical_interfaces = interfaces;
    std::ranges::sort(canonical_interfaces, [](const auto& left, const auto& right) {
        const auto compare_field = [](const std::string_view first, const std::string_view second) {
            if (first == second) {
                return 0;
            }
            return bytewise_less(first, second) ? -1 : 1;
        };
        if (const int order = compare_field(left.name, right.name); order != 0) {
            return order < 0;
        }
        if (const int order = compare_field(left.minus_phase, right.minus_phase); order != 0) {
            return order < 0;
        }
        if (const int order = compare_field(left.plus_phase, right.plus_phase); order != 0) {
            return order < 0;
        }
        return bytewise_less(left.operator_key.value(), right.operator_key.value());
    });
    return {std::move(canonical_phases), std::move(canonical_interfaces)};
}

/**
 * \brief Encode every logical graph-input field for exact collective comparison.
 * \param phases canonical phase declarations.
 * \param interfaces canonical interface declarations.
 * \param callback_available whether this rank supplied a compatibility callback.
 * \return unambiguous binary encoding; it is not a persistence format.
 */
std::string encode_graph_input(const std::vector<PhaseSpecification>& phases,
                               const std::vector<InterfaceSpecification>& interfaces, const bool callback_available)
{
    std::string output;
    append_uint64(output, phases.size());
    for (const auto& phase : phases) {
        append_length_prefixed(output, phase.name);
        append_length_prefixed(output, phase.physics_key.value());
    }
    append_uint64(output, interfaces.size());
    for (const auto& material_interface : interfaces) {
        append_length_prefixed(output, material_interface.name);
        append_length_prefixed(output, material_interface.minus_phase);
        append_length_prefixed(output, material_interface.plus_phase);
        append_length_prefixed(output, material_interface.operator_key.value());
    }
    output.push_back(callback_available ? '\x01' : '\x00');
    return output;
}

/**
 * \brief Adapt MPI allreduce to the private logical-disjunction operation.
 */
int mpi_collective_maximum(const std::uint64_t local, std::uint64_t& chosen, const MPI_Comm communicator)
{
    return MPI_Allreduce(&local, &chosen, 1, MPI_UINT64_T, MPI_MAX, communicator);
}

/** \brief Gather one encoded byte count from every rank. */
int mpi_gather_byte_counts(const std::uint64_t local_size, const std::span<std::uint64_t> lengths,
                           const MPI_Comm communicator)
{
    return MPI_Allgather(&local_size, 1, MPI_UINT64_T, lengths.data(), 1, MPI_UINT64_T, communicator);
}

/** \brief Gather every rank's exact encoded graph bytes. */
int mpi_gather_exact_bytes(const detail::ExactByteGatherRequest& request, const MPI_Comm communicator)
{
    return MPI_Allgatherv(request.local_bytes.data(), static_cast<int>(request.local_bytes.size()), MPI_BYTE,
                          request.gathered_bytes.data(), request.counts.data(), request.displacements.data(), MPI_BYTE,
                          communicator);
}

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
    std::map<std::string, PhaseId, BytewiseLess> ids;
};

/** \brief Count configuration positions for each non-empty name. */
using NameOccurrences = std::map<std::string, std::vector<std::size_t>, BytewiseLess>;

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
        const bool valid_name_utf8 = is_valid_utf8(phase.name);
        const bool valid_key_utf8 = is_valid_utf8(phase.physics_key.value());
        if (!valid_name_utf8) {
            add_error(errors, PhaseGraphErrorCode::invalid_utf8,
                      "phase at canonical index " + std::to_string(index) + " has invalid UTF-8 in its name");
        }
        if (!valid_key_utf8) {
            add_error(errors, PhaseGraphErrorCode::invalid_utf8,
                      "phase at canonical index " + std::to_string(index) + " has invalid UTF-8 in its physics key");
        }
        if (phase.name.empty()) {
            add_error(errors, PhaseGraphErrorCode::empty_phase_name,
                      "phase at canonical index " + std::to_string(index) + " has an empty name");
        }
        else if (valid_name_utf8) {
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
        if (specification.physics_key.value().empty() || !is_valid_utf8(specification.physics_key.value())) {
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
        const bool valid_name_utf8 = is_valid_utf8(material_interface.name);
        const bool valid_key_utf8 = is_valid_utf8(material_interface.operator_key.value());
        if (!valid_name_utf8) {
            add_error(errors, PhaseGraphErrorCode::invalid_utf8,
                      "interface at canonical index " + std::to_string(index) + " has invalid UTF-8 in its name");
        }
        if (!valid_key_utf8) {
            add_error(errors, PhaseGraphErrorCode::invalid_utf8,
                      "interface at canonical index " + std::to_string(index) +
                          " has invalid UTF-8 in its operator key");
        }
        if (material_interface.name.empty()) {
            add_error(errors, PhaseGraphErrorCode::empty_interface_name,
                      "interface at canonical index " + std::to_string(index) + " has an empty name");
        }
        else if (valid_name_utf8) {
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
                        const std::map<std::string, PhaseId, BytewiseLess>& phase_ids,
                        std::vector<ResolvedInterface>& resolved,
                        std::map<PhasePair, std::vector<std::size_t>>& pair_occurrences, PhaseGraphErrors& errors)
{
    for (std::size_t index = 0; index < specifications.size(); ++index) {
        const auto& specification = specifications.at(index);
        const bool valid_name_utf8 = is_valid_utf8(specification.name);
        const bool valid_minus_utf8 = is_valid_utf8(specification.minus_phase);
        const bool valid_plus_utf8 = is_valid_utf8(specification.plus_phase);
        const bool valid_key_utf8 = is_valid_utf8(specification.operator_key.value());
        if (!valid_minus_utf8) {
            add_error(errors, PhaseGraphErrorCode::invalid_utf8,
                      "interface at canonical index " + std::to_string(index) +
                          " has invalid UTF-8 in its minus-phase reference");
        }
        if (!valid_plus_utf8) {
            add_error(errors, PhaseGraphErrorCode::invalid_utf8,
                      "interface at canonical index " + std::to_string(index) +
                          " has invalid UTF-8 in its plus-phase reference");
        }

        bool valid = !specification.name.empty() && !specification.operator_key.value().empty() && valid_name_utf8 &&
                     valid_minus_utf8 && valid_plus_utf8 && valid_key_utf8;
        if (valid_name_utf8 && !specification.name.empty() &&
            interface_occurrences.at(specification.name).size() != 1) {
            valid = false;
        }

        if (valid_minus_utf8 && valid_plus_utf8 && specification.minus_phase == specification.plus_phase) {
            add_error(errors, PhaseGraphErrorCode::self_interface,
                      "interface '" + specification.name + "' joins phase '" + specification.minus_phase +
                          "' to itself");
            valid = false;
        }

        const auto minus = phase_ids.find(specification.minus_phase);
        if (valid_minus_utf8 && minus == phase_ids.end()) {
            add_error(errors, PhaseGraphErrorCode::missing_incident_phase,
                      "interface '" + specification.name + "' refers to missing or non-unique minus phase '" +
                          specification.minus_phase + "'");
            valid = false;
        }

        const auto plus = phase_ids.find(specification.plus_phase);
        if (valid_plus_utf8 && plus == phase_ids.end()) {
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
 * \param interfaces_supplied whether configuration contained any interface declaration.
 * \param communicator run communicator used to synchronize each callback step.
 * \param operations exact agreement, exception agreement, and fatal MPI operations.
 * \param rejection_recorder diagnostic operation executed after an agreed rejection.
 * \param errors collection receiving callback or missing-callback failures.
 */
void validate_compatibility(const std::vector<ResolvedInterface>& resolved, const std::vector<PhaseDescriptor>& phases,
                            const InterfaceCompatibilityCheck& compatibility_check, const bool interfaces_supplied,
                            const MPI_Comm communicator, const detail::PhaseGraphCollectiveOperations& operations,
                            const detail::CompatibilityRejectionRecorder rejection_recorder, PhaseGraphErrors& errors)
{
    if (interfaces_supplied && !compatibility_check) {
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
        std::optional<std::string> reason;
        std::exception_ptr exception;
        try {
            reason = compatibility_check(minus, plus, *material_interface.specification);
        }
        catch (...) {
            exception = std::current_exception();
        }

        if (detail::collective_any(communicator, exception != nullptr, operations)) {
            if (exception != nullptr) {
                throw CompatibilityCallbackException{.original = exception};
            }
            throw std::runtime_error("compatibility callback threw on another rank for interface '" +
                                     material_interface.specification->name + "'");
        }

        const std::string_view outcome = reason ? std::string_view{"rejected"} : std::string_view{"accepted"};
        if (!detail::collectively_equal_bytes(communicator, outcome, operations) ||
            (reason && !detail::collectively_equal_bytes(communicator, *reason, operations))) {
            add_error(errors, PhaseGraphErrorCode::collective_compatibility_mismatch,
                      "compatibility callback result differs across the run communicator for interface '" +
                          material_interface.specification->name + "'");
            return;
        }

        if (reason) {
            rejection_recorder(errors, *material_interface.specification, minus, plus, *reason);
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
        return bytewise_less(left.specification->name, right.specification->name);
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

bool collectively_equal_bytes(const MPI_Comm communicator, const std::string_view local_bytes,
                              const PhaseGraphCollectiveOperations& operations)
{
    int communicator_size = 0;
    int status = operations.communicator_size(communicator, &communicator_size);
    if (status != MPI_SUCCESS) {
        invoke_mpi_abort(operations.abort, communicator, status);
    }
    if (communicator_size <= 0 || local_bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        invoke_mpi_abort(operations.abort, communicator, MPI_ERR_COUNT);
    }

    std::vector<std::uint64_t> lengths;
    std::vector<int> counts;
    std::vector<int> displacements;
    try {
        lengths = operations.allocate_uint64_buffer(static_cast<std::size_t>(communicator_size));
        counts = operations.allocate_int_buffer(static_cast<std::size_t>(communicator_size));
        displacements = operations.allocate_int_buffer(static_cast<std::size_t>(communicator_size));
    }
    catch (const std::bad_alloc&) {
        invoke_mpi_abort(operations.abort, communicator, MPI_ERR_NO_MEM);
    }

    const std::uint64_t local_size = local_bytes.size();
    status = operations.gather_byte_counts(local_size, lengths, communicator);
    if (status != MPI_SUCCESS) {
        invoke_mpi_abort(operations.abort, communicator, status);
    }

    std::uint64_t total_size = 0;
    for (std::size_t rank = 0; rank < lengths.size(); ++rank) {
        if (lengths.at(rank) > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ||
            total_size > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) - lengths.at(rank)) {
            invoke_mpi_abort(operations.abort, communicator, MPI_ERR_COUNT);
        }
        counts.at(rank) = static_cast<int>(lengths.at(rank));
        displacements.at(rank) = static_cast<int>(total_size);
        total_size += lengths.at(rank);
    }

    std::string gathered;
    try {
        gathered = operations.allocate_byte_buffer(static_cast<std::size_t>(total_size));
    }
    catch (const std::bad_alloc&) {
        invoke_mpi_abort(operations.abort, communicator, MPI_ERR_NO_MEM);
    }
    status = operations.gather_exact_bytes({.local_bytes = local_bytes,
                                            .counts = counts,
                                            .displacements = displacements,
                                            .gathered_bytes = std::span{gathered}},
                                           communicator);
    if (status != MPI_SUCCESS) {
        invoke_mpi_abort(operations.abort, communicator, status);
    }

    for (std::size_t rank = 0; rank < lengths.size(); ++rank) {
        const auto start = static_cast<std::size_t>(displacements.at(rank));
        const auto count = static_cast<std::size_t>(counts.at(rank));
        if (std::string_view(gathered).substr(start, count) != local_bytes) {
            return false;
        }
    }
    return true;
}

bool collective_any(const MPI_Comm communicator, const bool local_condition,
                    const PhaseGraphCollectiveOperations& operations)
{
    const std::uint64_t local = local_condition ? 1U : 0U;
    std::uint64_t any = 0;
    const int status = operations.collective_maximum(local, any, communicator);
    if (status != MPI_SUCCESS) {
        invoke_mpi_abort(operations.abort, communicator, status);
    }
    return any != 0;
}

std::vector<std::uint64_t> allocate_uint64_buffer(const std::size_t size) { return std::vector<std::uint64_t>(size); }

std::vector<int> allocate_int_buffer(const std::size_t size) { return std::vector<int>(size); }

std::string allocate_byte_buffer(const std::size_t size)
{
    std::string buffer;
    buffer.resize(size);
    return buffer;
}

CanonicalPhaseGraphInput allocate_canonical_phase_graph_input(const std::vector<PhaseSpecification>& phases,
                                                              const std::vector<InterfaceSpecification>& interfaces,
                                                              const bool callback_available)
{
    auto [canonical_phases, canonical_interfaces] = canonicalize_input(phases, interfaces);
    auto encoded = encode_graph_input(canonical_phases, canonical_interfaces, callback_available);
    return {.phases = std::move(canonical_phases),
            .interfaces = std::move(canonical_interfaces),
            .encoded = std::move(encoded)};
}

void record_compatibility_rejection(PhaseGraphErrors& errors, const InterfaceSpecification& interface_specification,
                                    const PhaseDescriptor& minus, const PhaseDescriptor& plus,
                                    const std::string_view reason)
{
    add_error(errors, PhaseGraphErrorCode::incompatible_interface,
              "interface '" + interface_specification.name + "' is incompatible with minus phase '" + minus.name +
                  "' and plus phase '" + plus.name + "': " + std::string(reason));
}

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
 *     run_control, provenance, std::move(phases), std::move(interfaces));
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
     *     run_control, provenance,
     *     std::move(phases), std::move(interfaces));
     * \endcode
     *
     * No copies or additional validation are performed.
     *
     * \param run_control shared control retained for the graph lifetime.
     * \param provenance run and graph-instance identity assigned collectively.
     * \param phases phase descriptors indexed by `PhaseId`.
     * \param interfaces interface descriptors indexed by `InterfaceId`.
     * \return immutable runtime phase graph.
     */
    static PhaseGraph create(std::shared_ptr<const RunConfigurationControl> run_control,
                             const PhaseGraphProvenance provenance, std::vector<PhaseDescriptor> phases,
                             std::vector<InterfaceDescriptor> interfaces)
    {
        return {std::move(run_control), provenance, std::move(phases), std::move(interfaces)};
    }
};

} // namespace detail

PhaseGraph::PhaseGraph(std::shared_ptr<const detail::RunConfigurationControl> run_control,
                       const PhaseGraphProvenance provenance, std::vector<PhaseDescriptor> phases,
                       std::vector<InterfaceDescriptor> interfaces) :
    run_control_(std::move(run_control)),
    provenance_(provenance),
    phases_(std::move(phases)),
    interfaces_(std::move(interfaces))
{
}

std::expected<PhaseReference, PhaseGraphError> PhaseGraph::reference(const PhaseId id) const
{
    if (!std::cmp_less(id.value(), phases_.size())) {
        return std::unexpected(PhaseGraphError{.code = PhaseGraphErrorCode::invalid_phase_reference,
                                               .message = "phase ID " + std::to_string(id.value()) +
                                                          " is not present in this phase graph"});
    }
    return PhaseReference{.graph = provenance_, .phase = id};
}

bool PhaseGraph::owns(const PhaseReference& reference) const noexcept
{
    return reference.graph.run == provenance_.run && reference.graph.graph == provenance_.graph &&
           std::cmp_less(reference.phase.value(), phases_.size());
}

std::expected<std::reference_wrapper<const PhaseDescriptor>, PhaseGraphError>
PhaseGraph::phase(const PhaseReference& reference) const
{
    if (reference.graph.run != provenance_.run) {
        return std::unexpected(PhaseGraphError{.code = PhaseGraphErrorCode::foreign_run_reference,
                                               .message = "phase reference belongs to a different run configuration"});
    }
    if (reference.graph.graph != provenance_.graph) {
        return std::unexpected(PhaseGraphError{.code = PhaseGraphErrorCode::foreign_graph_reference,
                                               .message = "phase reference belongs to a different phase graph"});
    }
    if (!std::cmp_less(reference.phase.value(), phases_.size())) {
        return std::unexpected(PhaseGraphError{.code = PhaseGraphErrorCode::invalid_phase_reference,
                                               .message = "phase ID " + std::to_string(reference.phase.value()) +
                                                          " is not present in this phase graph"});
    }
    return std::cref(phases_.at(reference.phase.value()));
}

const PhaseDescriptor& PhaseGraph::phase(const PhaseId id) const { return phases_.at(id.value()); }

const InterfaceDescriptor& PhaseGraph::material_interface(const InterfaceId id) const
{
    return interfaces_.at(id.value());
}

std::optional<PhaseId> PhaseGraph::find_phase(const std::string_view name) const noexcept
{
    const auto found = std::ranges::lower_bound(phases_, name, BytewiseLess{}, &PhaseDescriptor::name);
    if (found == phases_.end() || found->name != name) {
        return std::nullopt;
    }

    return found->id;
}

std::optional<InterfaceId> PhaseGraph::find_interface(const std::string_view name) const noexcept
{
    const auto found = std::ranges::lower_bound(interfaces_, name, BytewiseLess{}, &InterfaceDescriptor::name);
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

PhaseGraphResult detail::make_phase_graph_with_input_allocator(
    const RunConfiguration& run, const std::vector<PhaseSpecification>& phase_specifications,
    const std::vector<InterfaceSpecification>& interface_specifications,
    const InterfaceCompatibilityCheck& compatibility_check, const CanonicalPhaseGraphInputAllocator input_allocator,
    const CompatibilityRejectionRecorder rejection_recorder)
{
    auto run_control = detail::RunConfigurationAccess::control(run);
    const MPI_Comm communicator = run_control->communicator();
    const detail::PhaseGraphCollectiveOperations operations{.communicator_size = MPI_Comm_size,
                                                            .gather_byte_counts = mpi_gather_byte_counts,
                                                            .gather_exact_bytes = mpi_gather_exact_bytes,
                                                            .allocate_uint64_buffer = detail::allocate_uint64_buffer,
                                                            .allocate_int_buffer = detail::allocate_int_buffer,
                                                            .allocate_byte_buffer = detail::allocate_byte_buffer,
                                                            .collective_maximum = mpi_collective_maximum,
                                                            .abort = run_control->abort_handler()};
    auto graph_id = run_control->reserve_graph_id();
    if (!graph_id) {
        return std::unexpected(PhaseGraphErrors{PhaseGraphError{.code = PhaseGraphErrorCode::graph_id_allocation_failed,
                                                                .message = std::move(graph_id).error().message}});
    }

    CanonicalPhaseGraphInput canonical_input;
    try {
        canonical_input = input_allocator(phase_specifications, interface_specifications, bool{compatibility_check});
    }
    catch (const std::bad_alloc&) {
        invoke_mpi_abort(operations.abort, communicator, MPI_ERR_NO_MEM);
    }
    if (!detail::collectively_equal_bytes(communicator, canonical_input.encoded, operations)) {
        return std::unexpected(
            PhaseGraphErrors{PhaseGraphError{.code = PhaseGraphErrorCode::collective_input_mismatch,
                                             .message = "phase graph input differs across the run communicator"}});
    }

    try {
        PhaseGraphErrors errors;
        auto validated_phases = validate_phases(canonical_input.phases, errors);
        const auto interface_occurrences = collect_interface_occurrences(canonical_input.interfaces, errors);
        std::vector<ResolvedInterface> resolved_interfaces;
        std::map<PhasePair, std::vector<std::size_t>> pair_occurrences;
        resolve_interfaces(canonical_input.interfaces, interface_occurrences, validated_phases.ids, resolved_interfaces,
                           pair_occurrences, errors);
        mark_duplicate_phase_pairs(resolved_interfaces, pair_occurrences, errors);
        validate_compatibility(resolved_interfaces, validated_phases.descriptors, compatibility_check,
                               !canonical_input.interfaces.empty(), communicator, operations, rejection_recorder,
                               errors);

        if (!errors.empty()) {
            return std::unexpected(std::move(errors));
        }

        auto interfaces = make_interface_descriptors(resolved_interfaces);
        const PhaseGraphProvenance provenance{.run = run.id(), .graph = PhaseGraphInstanceId::from_index(*graph_id)};
        return detail::PhaseGraphFactory::create(std::move(run_control), provenance,
                                                 std::move(validated_phases.descriptors), std::move(interfaces));
    }
    catch (const CompatibilityCallbackException& failure) {
        std::rethrow_exception(failure.original);
    }
    catch (const std::bad_alloc&) {
        invoke_mpi_abort(operations.abort, communicator, MPI_ERR_NO_MEM);
    }
}

PhaseGraphResult make_phase_graph(const RunConfiguration& run,
                                  const std::vector<PhaseSpecification>& phase_specifications,
                                  const std::vector<InterfaceSpecification>& interface_specifications,
                                  const InterfaceCompatibilityCheck& compatibility_check)
{
    return detail::make_phase_graph_with_input_allocator(run, phase_specifications, interface_specifications,
                                                         compatibility_check,
                                                         detail::allocate_canonical_phase_graph_input);
}

} // namespace rift
