#pragma once

/**
 * \file
 * \brief Value types used to configure and diagnose Rift's phase graph.
 */

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <rift/strong_id.hpp>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rift {

class RiftContext;

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
 * \brief Own all unresolved input needed to construct one canonical phase graph.
 *
 * This in an intermediate type and not intended to have a long lifetime.
 * This collects information parsed on a particular MPI rank and is passed
 * to a function that tests if all MPI ranks have the same configuration.
 *
 * \ingroup phase_graph
 */
struct PhaseGraphSpecification {
    /** \brief Named phase specifications in arbitrary declaration order. */
    std::vector<PhaseSpecification> phases;

    /** \brief Oriented interface specifications in arbitrary declaration order. */
    std::vector<InterfaceSpecification> interfaces;
};

/**
 * \brief Describe one phase in the published canonical graph.
 *
 * The graph owns its descriptors and exposes them only through const views.
 * A copied descriptor owns its name and registry key independently.
 *
 * \ingroup phase_graph
 */
struct PhaseDescriptor {
    /** \brief Contiguous canonical phase identifier. */
    PhaseId id;

    /** \brief Unique configuration name. */
    std::string name;

    /** \brief Registry key selecting this phase's compiled physics family. */
    PhysicsKey physics_key;
};

/**
 * \brief Describe one oriented interface in the published canonical graph.
 *
 * The resolved phase identifiers preserve the declared minus-to-plus
 * orientation. The graph owns its descriptors and exposes them only through
 * const views.
 *
 * \ingroup phase_graph
 */
struct InterfaceDescriptor {
    /** \brief Contiguous canonical interface identifier. */
    InterfaceId id;

    /** \brief Unique configuration name. */
    std::string name;

    /** \brief Resolved identifier of the declared minus phase. */
    PhaseId minus_phase;

    /** \brief Resolved identifier of the declared plus phase. */
    PhaseId plus_phase;

    /** \brief Registry key selecting the complete pairwise interface law. */
    InterfaceOperatorKey operator_key;
};

/**
 * \brief Type that indicates whether an interface is accepted.
 *
 * A value accepts the interface. An `std::unexpected<std::string>` rejects it;
 * Relevant specification fields are included in the rejection message.
 *
 * \ingroup phase_graph
 */
using InterfaceCompatibilityDecision = std::expected<void, std::string>;

/**
 * \brief Function type that tests whether an interface is compatible.
 *
 * The first two arguments are the resolved minus- and plus-phase
 * specifications, respectively. The third argument is the relevant interface specification.
 * This returns @ref InterfaceCompatibilityDecision, which indicates whether the interface is accepted or rejected.
 *
 * \ingroup phase_graph
 */
using InterfaceCompatibilityTest = std::function<InterfaceCompatibilityDecision(
    const PhaseSpecification&, const PhaseSpecification&, const InterfaceSpecification&)>;

/** \brief Explicit application-independent interface compatibility policies. */
namespace interface_compatibility {

/**
 * \brief Accept every structurally valid interface configuration.
 *
 * Use this policy when there are no particular restrictions on the interface operator and phase physics families.
 *
 * \return an accepting compatibility decision.
 * \ingroup phase_graph
 */
[[nodiscard]] inline InterfaceCompatibilityDecision accept_all(const PhaseSpecification& /* minus_phase */,
                                                               const PhaseSpecification& /* plus_phase */,
                                                               const InterfaceSpecification& /* interface */) noexcept
{
    return {};
}

} // namespace interface_compatibility

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
    /** \brief The compatibility query threw while examining an interface. */
    compatibility_test_exception,
    /** \brief Phase-graph creation was requested after the context's single attempt. */
    phase_graph_creation_already_attempted,
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

/**
 * \brief Immutable canonical phase graph owned by one Rift context.
 *
 * Construction is available only through `RiftContext::create_phase_graph()`.
 * Consumers borrow this object from its context; it cannot be copied, moved,
 * or assigned.
 *
 * \ingroup phase_graph
 */
class PhaseGraph {
public:
    /**
     * \brief Read every phase in ascending canonical ID order.
     *
     * \return borrowed immutable view valid for this graph's lifetime.
     */
    [[nodiscard]] std::span<const PhaseDescriptor> phases() const noexcept;

    /**
     * \brief Read every interface in ascending canonical ID order.
     *
     * \return borrowed immutable view valid for this graph's lifetime.
     */
    [[nodiscard]] std::span<const InterfaceDescriptor> interfaces() const noexcept;

    /**
     * \brief Resolve a phase ID in constant time.
     *
     * \param id canonical phase identifier.
     * \return immutable graph-owned phase descriptor.
     * \throws std::out_of_range when `id` is absent from this graph.
     */
    [[nodiscard]] const PhaseDescriptor& phase(PhaseId id) const;

    /**
     * \brief Resolve an interface ID in constant time.
     *
     * \param id canonical interface identifier.
     * \return immutable graph-owned interface descriptor.
     * \throws std::out_of_range when `id` is absent from this graph.
     */
    [[nodiscard]] const InterfaceDescriptor& material_interface(InterfaceId id) const;

    /**
     * \brief Find a phase by its exact configuration name.
     *
     * \param name phase name to match.
     * \return canonical phase ID, or no value when the name is absent.
     */
    [[nodiscard]] std::optional<PhaseId> find_phase(std::string_view name) const noexcept;

    /**
     * \brief Find an interface by its exact configuration name.
     *
     * \param name interface name to match.
     * \return canonical interface ID, or no value when the name is absent.
     */
    [[nodiscard]] std::optional<InterfaceId> find_interface(std::string_view name) const noexcept;

    /**
     * \brief Find the sole interface joining an unordered phase pair.
     *
     * The returned descriptor retains its declared minus-to-plus orientation.
     *
     * \param first either incident phase.
     * \param second the other incident phase.
     * \return canonical interface ID, or no value for an identical, invalid,
     * nonadjacent pair.
     */
    [[nodiscard]] std::optional<InterfaceId> find_interface(PhaseId first, PhaseId second) const noexcept;

    /** \brief Destroy the private immutable graph storage. */
    ~PhaseGraph();

    /** \brief Copy construction is disabled because the context is the sole owner. */
    PhaseGraph(const PhaseGraph&) = delete;

    /** \brief Copy assignment is disabled because the context is the sole owner. */
    PhaseGraph& operator=(const PhaseGraph&) = delete;

    /** \brief Move construction is disabled to keep borrowed references stable. */
    PhaseGraph(PhaseGraph&&) = delete;

    /** \brief Move assignment is disabled to keep borrowed references stable. */
    PhaseGraph& operator=(PhaseGraph&&) = delete;

private:
    /** \brief Opaque canonical phase and interface storage. */
    struct Storage;

    /** \brief Adopt collectively agreed immutable storage. */
    explicit PhaseGraph(std::unique_ptr<const Storage> storage);

    /** \brief Immutable storage owned for this graph's entire lifetime. */
    std::unique_ptr<const Storage> storage_;

    friend class RiftContext;
};

/**
 * \brief Result of the context's single collective graph-creation attempt.
 *
 * A successful value borrows the context-owned immutable graph. The reference
 * remains valid until the owning `RiftContext` is destroyed.
 *
 * \ingroup phase_graph
 */
using PhaseGraphResult = std::expected<std::reference_wrapper<const PhaseGraph>, PhaseGraphErrors>;

} // namespace rift
