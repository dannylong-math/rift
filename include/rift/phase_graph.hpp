#pragma once

/**
 * \file
 * \brief Value types used to configure and diagnose Rift's phase graph.
 */

#include <compare>
#include <cstdint>
#include <rift/strong_id.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rift {

/**
 * \defgroup phase_graph Runtime phase graph
 * \brief Named phases, oriented interfaces, and their stable identifiers.
 */

/** \brief Implementation tags and storage used by public phase-graph values. */
namespace detail {

/** \brief Distinguish phase identifiers from other strong identifiers. */
struct PhaseIdTag {};

/** \brief Distinguish interface identifiers from other strong identifiers. */
struct InterfaceIdTag {};

/** \brief Distinguish phase-physics registry keys from other runtime keys. */
struct PhysicsKeyTag {};

/** \brief Distinguish interface-operator registry keys from other runtime keys. */
struct InterfaceOperatorKeyTag {};

/**
 * \brief Own a registry spelling in one compile-time registry domain.
 *
 * Empty and otherwise invalid spellings remain representable so later graph
 * construction can collect configuration errors. The key preserves the exact
 * byte spelling supplied by the caller.
 *
 * \tparam Tag registry domain that prevents unrelated keys from mixing.
 */
template<class Tag> class RuntimeKey {
public:
    /**
     * \brief Capture a registry spelling exactly as supplied.
     *
     * \param value spelling to own, including an empty spelling for later
     * validation.
     */
    explicit RuntimeKey(std::string value) : value_(std::move(value)) {}

    /**
     * \brief Read the owned registry spelling.
     *
     * \return borrowed view valid while this key remains alive and unmodified.
     */
    [[nodiscard]] std::string_view value() const noexcept { return value_; }

    /**
     * \brief Compare keys from the same registry domain.
     *
     * \return ordering of their exact registry spellings.
     */
    friend auto operator<=>(const RuntimeKey&, const RuntimeKey&) = default;

private:
    /** \brief Exact owned registry spelling. */
    std::string value_;
};

} // namespace detail

/**
 * \brief Identify one phase in the executable's canonical phase graph.
 * \ingroup phase_graph
 */
using PhaseId = StrongId<detail::PhaseIdTag>;

/**
 * \brief Identify one interface in the executable's canonical phase graph.
 * \ingroup phase_graph
 */
using InterfaceId = StrongId<detail::InterfaceIdTag>;

/**
 * \brief Select a compiled phase-physics family by registry name.
 * \ingroup phase_graph
 */
using PhysicsKey = detail::RuntimeKey<detail::PhysicsKeyTag>;

/**
 * \brief Select a compiled interface operator by registry name.
 * \ingroup phase_graph
 */
using InterfaceOperatorKey = detail::RuntimeKey<detail::InterfaceOperatorKeyTag>;

/**
 * \brief Describe one named phase before canonical IDs are assigned.
 *
 * The value owns its exact input spellings. Empty names and keys remain
 * representable so graph construction can report them as collected errors.
 *
 * \ingroup phase_graph
 */
struct PhaseSpecification {
    /** \brief Configuration name used to identify this phase. */
    std::string name;

    /** \brief Registry key selecting this phase's compiled physics family. */
    PhysicsKey physics_key;
};

/**
 * \brief Describe one named, oriented interface before canonical IDs are assigned.
 *
 * The value owns every input spelling. `minus_phase` and `plus_phase` preserve
 * the caller's physical orientation exactly; graph construction must not sort
 * or infer their order. Invalid and empty values remain representable for
 * collected validation.
 *
 * \ingroup phase_graph
 */
struct InterfaceSpecification {
    /** \brief Configuration name used to identify this interface. */
    std::string name;

    /** \brief Name of the phase on the declared minus side. */
    std::string minus_phase;

    /** \brief Name of the phase on the declared plus side. */
    std::string plus_phase;

    /** \brief Registry key selecting the complete pairwise interface law. */
    InterfaceOperatorKey operator_key;
};

/**
 * \brief Classify a recoverable phase-graph configuration defect.
 *
 * Local codes describe one rank's configuration. Collective codes describe
 * exact disagreement between otherwise participating `MPI_COMM_WORLD` ranks.
 *
 * \ingroup phase_graph
 */
enum class PhaseGraphErrorCode : std::uint8_t {
    /** \brief No phase specifications were supplied. */
    no_phases,
    /** \brief A phase specification has an empty name. */
    empty_phase_name,
    /** \brief A phase specification has an empty physics key. */
    empty_physics_key,
    /** \brief More than one phase uses the same name. */
    duplicate_phase_name,
    /** \brief An interface specification has an empty name. */
    empty_interface_name,
    /** \brief An interface specification has an empty operator key. */
    empty_interface_operator_key,
    /** \brief More than one interface uses the same name. */
    duplicate_interface_name,
    /** \brief An interface names a phase absent from the phase specifications. */
    missing_incident_phase,
    /** \brief An interface names the same phase on both sides. */
    self_interface,
    /** \brief More than one interface joins the same unordered phase pair. */
    duplicate_phase_pair,
    /** \brief Interface specifications were supplied without a compatibility check. */
    missing_compatibility_check,
    /** \brief A name or registry key is not well-formed UTF-8. */
    invalid_utf8,
    /** \brief The compatibility query rejected a phase/operator combination. */
    incompatible_interface,
    /** \brief Canonical graph input or local status differs across world ranks. */
    collective_input_mismatch,
    /** \brief Compatibility acceptance or rejection differs across world ranks. */
    collective_compatibility_mismatch,
};

/**
 * \brief Pair a machine-readable graph error code with a human-readable message.
 * \ingroup phase_graph
 */
struct PhaseGraphError {
    /** \brief Machine-readable error classification. */
    PhaseGraphErrorCode code;

    /** \brief Human-readable diagnostic naming relevant configuration values. */
    std::string message;
};

/**
 * \brief Collect independent phase-graph defects from one construction attempt.
 * \ingroup phase_graph
 */
using PhaseGraphErrors = std::vector<PhaseGraphError>;

} // namespace rift
