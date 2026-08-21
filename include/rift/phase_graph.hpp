#pragma once

/**
 * \file
 * \brief Stable identities, specifications, validation, and lookup for the runtime phase graph.
 */

#include <compare>
#include <concepts>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * \brief Types and functions provided by the Rift solver library.
 */
namespace rift {

/**
 * \defgroup phase_graph Runtime phase graph
 * \brief Named phase instances, oriented material-interface edges, and their stable identities.
 */

/**
 * \brief Give a compact integer identifier its own compile-time semantic type.
 *
 * \par When to use
 * Define a `StrongId` alias when two integer identifiers have different
 * meanings and must not be mixed accidentally. Rift uses this template for
 * `PhaseId` and `InterfaceId`; other subsystems can define their own tag and
 * obtain the same zero-overhead type safety.
 *
 * \par Typical use
 * \code{.cpp}
 * struct CellIdTag {};
 * using CellId = rift::StrongId<CellIdTag>;
 *
 * const CellId first = CellId::from_index(0);
 * const CellId second = CellId::from_index(1);
 *
 * const std::uint32_t first_index = first.value();
 * const bool ids_are_ordered = first < second;
 * \endcode
 *
 * \par Important behavior
 * The tag occupies no storage, so a `StrongId` has the same size as
 * `Representation`. Comparisons are available only between identifiers with
 * the same tag. Integer conversion is explicit through `from_index()` and
 * `value()`.
 *
 * \tparam Tag semantic domain that distinguishes this identifier from other identifiers.
 * \tparam Representation unsigned integer type used for storage.
 * \ingroup phase_graph
 */
template<class Tag, std::unsigned_integral Representation = std::uint32_t> class StrongId {
public:
    /**
     * \brief Unsigned integer type used to store this identifier.
     */
    using representation_type = Representation;

    /**
     * \brief Recreate an identifier from an index owned by the same subsystem.
     *
     * Graph construction uses this factory while assigning canonical IDs. Code
     * reading an arbitrary integer should validate that it is in range before
     * using the result with an indexed lookup:
     *
     * \code{.cpp}
     * const rift::PhaseId phase = rift::PhaseId::from_index(0);
     * const rift::PhaseDescriptor &descriptor = graph.phase(phase);
     * \endcode
     *
     * \param value zero-based integer representation.
     * \return identifier containing `value`.
     */
    static constexpr StrongId from_index(const Representation value) noexcept { return StrongId(value); }

    /**
     * \brief Use the identifier as an index in storage owned by its subsystem.
     *
     * \code{.cpp}
     * const rift::PhaseId phase = graph.find_phase("gas").value();
     * const rift::PhaseDescriptor &same_phase = graph.phases().at(phase.value());
     * \endcode
     *
     * Do not compare this raw value with integers from a different graph or
     * identifier domain.
     *
     * \return stored integer value.
     */
    [[nodiscard]] constexpr Representation value() const noexcept { return value_; }

    /**
     * \brief Compare two identifiers from the same semantic domain.
     *
     * \return strong ordering of the stored integer representations.
     */
    friend constexpr auto operator<=>(const StrongId&, const StrongId&) noexcept = default;

private:
    /**
     * \brief Store a validated subsystem index without permitting implicit conversion.
     *
     * Maintainers invoke this constructor only through `from_index()`, keeping
     * the public conversion explicit and searchable.
     *
     * \param value zero-based integer representation to store.
     */
    explicit constexpr StrongId(const Representation value) noexcept : value_(value) {}

    /**
     * \brief Zero-based integer representation.
     */
    Representation value_;
};

/**
 * \brief Implementation types used to distinguish and construct phase-graph values.
 *
 * These types are documented because they participate in compile-time type
 * identity and controlled construction, even though application code should
 * normally use the public aliases and factories.
 *
 * \ingroup phase_graph
 */
namespace detail {

/**
 * \brief Make `PhaseId` a distinct strong-identifier type.
 *
 * \par When to use
 * Application code uses `PhaseId`, not this tag directly. Maintainers name
 * this type only when defining the public alias or generic code that inspects
 * its semantic tag.
 *
 * \par Typical use
 * \code{.cpp}
 * using PhaseId = rift::StrongId<rift::detail::PhaseIdTag>;
 * const PhaseId phase = PhaseId::from_index(0);
 *
 * const std::uint32_t phase_index = phase.value();
 * \endcode
 *
 * \par Important behavior
 * The empty tag contributes compile-time identity but no runtime state.
 * \ingroup phase_graph
 */
struct PhaseIdTag {};

/**
 * \brief Make `InterfaceId` a distinct strong-identifier type.
 *
 * \par When to use
 * Application code uses `InterfaceId`, not this tag directly. The separate tag
 * prevents an interface identifier from being passed where a phase identifier
 * is required.
 *
 * \par Typical use
 * \code{.cpp}
 * using InterfaceId = rift::StrongId<rift::detail::InterfaceIdTag>;
 * const InterfaceId interface_id = InterfaceId::from_index(0);
 *
 * const std::uint32_t interface_index = interface_id.value();
 * \endcode
 *
 * \par Important behavior
 * The empty tag contributes compile-time identity but no runtime state.
 * \ingroup phase_graph
 */
struct InterfaceIdTag {};

/**
 * \brief Make `PhysicsKey` a distinct runtime-key type.
 *
 * \par When to use
 * Registry and graph code use this tag through the `PhysicsKey` alias. It
 * prevents a phase-physics key from being confused with an interface-operator
 * key even though both store strings.
 *
 * \par Typical use
 * \code{.cpp}
 * using PhysicsKey = rift::RuntimeKey<rift::detail::PhysicsKeyTag>;
 * const PhysicsKey physics{"compressible"};
 *
 * const std::string_view configured_name = physics.value();
 * \endcode
 *
 * \par Important behavior
 * The tag contributes only compile-time identity; the associated `RuntimeKey`
 * owns the runtime string.
 * \ingroup phase_graph
 */
struct PhysicsKeyTag {};

/**
 * \brief Make `InterfaceOperatorKey` a distinct runtime-key type.
 *
 * \par When to use
 * Registry and graph code use this tag through `InterfaceOperatorKey`. It
 * prevents an operator selection from being passed to an API expecting a
 * phase-physics selection.
 *
 * \par Typical use
 * \code{.cpp}
 * using InterfaceOperatorKey =
 *     rift::RuntimeKey<rift::detail::InterfaceOperatorKeyTag>;
 * const InterfaceOperatorKey law{"finite-rate"};
 *
 * const std::string_view configured_name = law.value();
 * \endcode
 *
 * \par Important behavior
 * The tag contributes only compile-time identity; the associated `RuntimeKey`
 * owns the runtime string.
 * \ingroup phase_graph
 */
struct InterfaceOperatorKeyTag {};

/**
 * \brief Let graph validation construct `PhaseGraph` without exposing an unsafe public constructor.
 *
 * \par When to use
 * This is a maintainer-only gateway. `make_phase_graph()` validates and
 * canonicalizes user specifications, then delegates its final construction
 * step to this factory. Application code must call `make_phase_graph()`.
 *
 * \par Typical use
 * \code{.cpp}
 * // Inside make_phase_graph(), after every validation step has succeeded:
 * std::vector<PhaseDescriptor> phases{{
 *     .id = PhaseId::from_index(0),
 *     .name = "gas",
 *     .physics_key = PhysicsKey{"compressible"},
 * }};
 * std::vector<InterfaceDescriptor> interfaces;
 * return detail::PhaseGraphFactory::create(
 *     std::move(phases), std::move(interfaces));
 * \endcode
 *
 * \par Important behavior
 * Keeping the definition in the implementation file prevents unrelated code
 * from bypassing validation while still allowing `PhaseGraph` to keep its
 * constructor private.
 * \ingroup phase_graph
 */
struct PhaseGraphFactory;

} // namespace detail

/**
 * \brief Stable numeric identity of a named phase in one configured graph.
 * \ingroup phase_graph
 */
using PhaseId = StrongId<detail::PhaseIdTag>;

/**
 * \brief Stable numeric identity of a named material interface in one configured graph.
 * \ingroup phase_graph
 */
using InterfaceId = StrongId<detail::InterfaceIdTag>;

/**
 * \brief Store a registry name while preventing keys from different registries from being mixed.
 *
 * \par When to use
 * Define a `RuntimeKey` alias for each registry whose values are represented by
 * strings. Rift uses `PhysicsKey` for phase physics and
 * `InterfaceOperatorKey` for interface laws.
 *
 * \par Typical use
 * \code{.cpp}
 * const rift::PhysicsKey physics{"compressible"};
 * const rift::InterfaceOperatorKey interface_law{"finite-rate"};
 *
 * const std::string_view physics_name = physics.value();
 * const std::string_view law_name = interface_law.value();
 * // physics == interface_law; // Does not compile: different key types.
 * \endcode
 *
 * \par Important behavior
 * The key owns its string and returns a non-owning view from `value()`. Empty
 * keys remain representable so the graph builder can report them alongside
 * other configuration errors.
 *
 * \tparam Tag semantic domain that prevents accidental mixing with other runtime keys.
 * \ingroup phase_graph
 */
template<class Tag> class RuntimeKey {
public:
    /**
     * \brief Capture the exact spelling used to select a registry entry.
     *
     * Empty keys are representable so graph construction can return a structured
     * validation error rather than throwing from this value type.
     *
     * \code{.cpp}
     * const rift::PhysicsKey physics{"compressible"};
     * const rift::PhaseSpecification gas{"gas", std::string{physics.value()}};
     * \endcode
     *
     * \param value registry spelling to store.
     */
    explicit RuntimeKey(std::string value) : value_(std::move(value)) {}

    /**
     * \brief Compare or pass the key spelling to the registry that owns it.
     *
     * \code{.cpp}
     * const rift::PhysicsKey physics{"compressible"};
     * if (physics.value() == "compressible")
     *     std::cout << "compressible phase" << '\n';
     * \endcode
     *
     * \return non-owning view valid for the lifetime of this key.
     */
    [[nodiscard]] std::string_view value() const noexcept { return value_; }

    /**
     * \brief Compare two keys from the same semantic domain.
     *
     * \return ordering of their registry spellings.
     */
    friend auto operator<=>(const RuntimeKey&, const RuntimeKey&) = default;

private:
    /**
     * \brief Owned registry spelling.
     */
    std::string value_;
};

/**
 * \brief Key selecting a compiled phase-physics family at run construction.
 * \ingroup phase_graph
 */
using PhysicsKey = RuntimeKey<detail::PhysicsKeyTag>;

/**
 * \brief Key selecting a compiled pairwise interface operator at run construction.
 * \ingroup phase_graph
 */
using InterfaceOperatorKey = RuntimeKey<detail::InterfaceOperatorKeyTag>;

/**
 * \brief Describe one named phase before the graph assigns its stable identity.
 *
 * \par When to use
 * Create these values while translating user configuration into the phase
 * graph. The name is the configuration-time identity referenced by
 * `InterfaceSpecification`; the physics key selects a compiled phase family.
 *
 * \par Typical use
 * \code{.cpp}
 * std::vector<rift::PhaseSpecification> phases{
 *     {"gas", "compressible"},
 *     {"liquid", "low-mach"},
 * };
 *
 * const auto result = rift::make_phase_graph(std::move(phases), {});
 * if (!result) {
 *     for (const auto &error : result.error())
 *         std::cerr << error.message << '\n';
 * }
 * \endcode
 *
 * \par Important behavior
 * This is an unresolved input value. Successful graph construction replaces
 * name-based references with `PhaseId`; numerical code should use the resolved
 * descriptors rather than retaining specifications.
 *
 * \par Failure handling
 * Empty names, duplicate names, and empty physics keys are accepted by the
 * value type and reported together by `make_phase_graph()`.
 * \ingroup phase_graph
 */
struct PhaseSpecification {
    /**
     * \brief Pair a unique phase name with the physics registry key it selects.
     *
     * Prefer constructing specifications directly in the vector passed to
     * `make_phase_graph()`:
     *
     * \code{.cpp}
     * const auto result =
     *     rift::make_phase_graph({{"gas", "compressible"}}, {});
     * \endcode
     *
     * \param name unique configuration name of the phase.
     * \param physics_key registry key selecting the phase physics.
     */
    PhaseSpecification(std::string name, std::string physics_key) :
        name(std::move(name)), physics_key(PhysicsKey(std::move(physics_key)))
    {
    }

    /**
     * \brief Unique configuration name used for diagnostics and canonical identity.
     */
    std::string name;

    /**
     * \brief Registry key selecting the phase's fixed physics family.
     */
    PhysicsKey physics_key;
};

/**
 * \brief Declare one oriented material interface between two named phases.
 *
 * \par When to use
 * Create interface specifications alongside `PhaseSpecification` values while
 * translating configuration. `minus_phase` and `plus_phase` reference phase
 * names from that same construction request.
 *
 * \par Typical use
 * \code{.cpp}
 * const rift::InterfaceCompatibilityCheck accept_pair =
 *     [](const rift::PhaseDescriptor &,
 *        const rift::PhaseDescriptor &,
 *        const rift::InterfaceSpecification &) {
 *         return std::optional<std::string>{};
 *     };
 *
 * const auto result = rift::make_phase_graph(
 *     {{"gas", "compressible"}, {"liquid", "low-mach"}},
 *     {{"surface", "liquid", "gas", "finite-rate"}},
 *     accept_pair);
 *
 * if (!result) {
 *     for (const auto &error : result.error())
 *         std::cerr << error.message << '\n';
 * }
 * \endcode
 *
 * \par Important behavior
 * Minus/plus order is physical and survives name resolution unchanged. It is
 * never inferred from declaration order, lexical name order, or numeric IDs.
 * The initial graph admits at most one edge for an unordered phase pair.
 *
 * \par Failure handling
 * Graph construction reports missing phases, self-interfaces, duplicate names
 * or phase pairs, empty operator keys, and compatibility rejection.
 * \ingroup phase_graph
 */
struct InterfaceSpecification {
    /**
     * \brief Pair two phase names with an explicitly oriented interface law.
     *
     * The order of the two phase arguments becomes the persistent minus/plus
     * orientation:
     *
     * \code{.cpp}
     * const rift::InterfaceSpecification surface{
     *     "surface", "liquid", "gas", "finite-rate"};
     * \endcode
     *
     * \param name unique configuration name of the interface.
     * \param minus_phase name of the phase on the canonical minus side.
     * \param plus_phase name of the phase on the canonical plus side.
     * \param operator_key registry key selecting the pairwise interface operator.
     */
    InterfaceSpecification(std::string name, std::string minus_phase, std::string plus_phase,
                           std::string operator_key) :
        name(std::move(name)),
        minus_phase(std::move(minus_phase)),
        plus_phase(std::move(plus_phase)),
        operator_key(InterfaceOperatorKey(std::move(operator_key)))
    {
    }

    /**
     * \brief Unique configuration name used for diagnostics and canonical identity.
     */
    std::string name;

    /**
     * \brief Configuration name of the phase on the canonical minus side.
     */
    std::string minus_phase;

    /**
     * \brief Configuration name of the phase on the canonical plus side.
     */
    std::string plus_phase;

    /**
     * \brief Registry key selecting the pairwise interface operator.
     */
    InterfaceOperatorKey operator_key;
};

/**
 * \brief Read the stable identity and fixed physics selection of a graph phase.
 *
 * \par When to use
 * Obtain descriptors from `PhaseGraph` after construction. Configuration and
 * diagnostic code may inspect the name and key; numerical routing should keep
 * the compact `id`.
 *
 * \par Typical use
 * \code{.cpp}
 * const auto result = rift::make_phase_graph({{"gas", "compressible"}}, {});
 * if (result) {
 *     const rift::PhaseId gas_id = result->find_phase("gas").value();
 *     const rift::PhaseDescriptor &gas = result->phase(gas_id);
 *     const std::string_view physics = gas.physics_key.value();
 * }
 * \endcode
 *
 * \par Important behavior
 * Descriptors are owned by `PhaseGraph`. References and string views obtained
 * from them remain valid only while that graph remains alive.
 * \ingroup phase_graph
 */
struct PhaseDescriptor {
    /**
     * \brief Canonical numeric phase identity.
     */
    PhaseId id;

    /**
     * \brief Unique configuration name.
     */
    std::string name;

    /**
     * \brief Registry key selecting the phase's fixed physics family.
     */
    PhysicsKey physics_key;
};

/**
 * \brief Read the stable orientation and operator selection of a graph edge.
 *
 * \par When to use
 * Obtain interface descriptors from `PhaseGraph` when binding interface
 * operators or routing interface work. Resolve the incident phase descriptors
 * through `minus_phase` and `plus_phase`.
 *
 * \par Typical use
 * \code{.cpp}
 * const rift::InterfaceCompatibilityCheck accept_pair =
 *     [](const auto &, const auto &, const auto &) {
 *         return std::optional<std::string>{};
 *     };
 * const auto result = rift::make_phase_graph(
 *     {{"gas", "compressible"}, {"liquid", "low-mach"}},
 *     {{"surface", "liquid", "gas", "finite-rate"}},
 *     accept_pair);
 * if (result) {
 *     const rift::InterfaceId id = result->find_interface("surface").value();
 *     const rift::InterfaceDescriptor &edge = result->material_interface(id);
 *     const rift::PhaseDescriptor &minus = result->phase(edge.minus_phase);
 *     const rift::PhaseDescriptor &plus = result->phase(edge.plus_phase);
 * }
 * \endcode
 *
 * \par Important behavior
 * The incident IDs preserve declared minus/plus orientation. The descriptor is
 * owned by `PhaseGraph` and remains immutable for the graph's lifetime.
 * \ingroup phase_graph
 */
struct InterfaceDescriptor {
    /**
     * \brief Canonical numeric interface identity.
     */
    InterfaceId id;

    /**
     * \brief Unique configuration name.
     */
    std::string name;

    /**
     * \brief Resolved phase identity on the canonical minus side.
     */
    PhaseId minus_phase;

    /**
     * \brief Resolved phase identity on the canonical plus side.
     */
    PhaseId plus_phase;

    /**
     * \brief Registry key selecting the pairwise interface operator.
     */
    InterfaceOperatorKey operator_key;
};

/**
 * \brief Select programmatic recovery or reporting for graph-construction errors.
 *
 * Use the code when diagnostics need machine-readable handling; use
 * `PhaseGraphError::message` when reporting the problem to a person. One
 * construction attempt may contain more than one code.
 * \ingroup phase_graph
 */
enum class PhaseGraphErrorCode : std::uint8_t{
    /**
     * \brief No phase specifications were supplied.
     */
    no_phases,

    /**
     * \brief A phase specification has an empty name.
     */
    empty_phase_name,

    /**
     * \brief A phase specification has an empty physics key.
     */
    empty_physics_key,

    /**
     * \brief More than one phase uses the same configuration name.
     */
    duplicate_phase_name,

    /**
     * \brief An interface specification has an empty name.
     */
    empty_interface_name,

    /**
     * \brief An interface specification has an empty operator key.
     */
    empty_interface_operator_key,

    /**
     * \brief More than one interface uses the same configuration name.
     */
    duplicate_interface_name,

    /**
     * \brief An interface names a phase that is absent or non-unique.
     */
    missing_incident_phase,

    /**
     * \brief An interface uses the same phase on both sides.
     */
    self_interface,

    /**
     * \brief More than one interface joins the same unordered phase pair.
     */
    duplicate_phase_pair,

    /**
     * \brief An interface-bearing graph was built without a compatibility callback.
     */
    missing_compatibility_check,

    /**
     * \brief The compatibility callback rejected an ordered phase/operator combination.
     */
    incompatible_interface,
};

/**
 * \brief Inspect and report one phase-graph construction failure.
 *
 * \par When to use
 * Read these values from the error branch of `PhaseGraphResult`. Use `code` for
 * programmatic handling and `message` for user-facing diagnostics.
 *
 * \par Typical use
 * \code{.cpp}
 * const auto result = rift::make_phase_graph(
 *     {{"gas", "compressible"}, {"gas", "low-mach"}}, {});
 *
 * if (!result) {
 *     for (const rift::PhaseGraphError &error : result.error()) {
 *         std::cerr << static_cast<int>(error.code)
 *                   << ": " << error.message << '\n';
 *     }
 * }
 * \endcode
 *
 * \par Important behavior
 * One construction attempt may return several diagnostics so users can repair
 * independent configuration problems in one pass. Error messages name the
 * relevant phase or interface whenever one exists.
 * \ingroup phase_graph
 */
struct PhaseGraphError {
    /**
     * \brief Machine-readable error classification.
     */
    PhaseGraphErrorCode code;

    /**
     * \brief Human-readable diagnostic naming the relevant configuration entities.
     */
    std::string message;
};

/**
 * \brief Collection returned when phase-graph construction detects one or more errors.
 * \ingroup phase_graph
 */
using PhaseGraphErrors = std::vector<PhaseGraphError>;

/**
 * \brief Let the compiled registry accept or explain rejection of an interface pairing.
 *
 * \par When to use
 * Supply this callback whenever a graph contains interfaces. It is the seam
 * between generic graph validation and the registry that knows which ordered
 * physics/operator combinations were compiled.
 *
 * \par Typical use
 * \code{.cpp}
 * const rift::InterfaceCompatibilityCheck check =
 *     [](const rift::PhaseDescriptor &minus,
 *        const rift::PhaseDescriptor &plus,
 *        const rift::InterfaceSpecification &interface)
 *         -> std::optional<std::string> {
 *         if (interface.operator_key.value() != "finite-rate")
 *             return "operator is not registered";
 *         if (minus.physics_key.value() == plus.physics_key.value())
 *             return "this law requires different phase physics for each phase";
 *         return std::nullopt;
 *     };
 *
 * const auto result = rift::make_phase_graph(
 *     {{"gas", "compressible"}, {"liquid", "low-mach"}},
 *     {{"surface", "liquid", "gas", "finite-rate"}},
 *     check);
 * \endcode
 *
 * \par Important behavior
 * The callback runs only during construction, once per structurally valid
 * interface. Returning `std::nullopt` accepts the pairing; returning a string
 * rejects it and appends that reason to a named graph diagnostic.
 * \ingroup phase_graph
 */
using InterfaceCompatibilityCheck = std::function<std::optional<std::string>(
    const PhaseDescriptor&, const PhaseDescriptor&, const InterfaceSpecification&)>;

/**
 * \brief Build once and query the stable phase and interface topology of a run.
 *
 * \par When to use
 * Construct a graph after parsing configuration and resolving the available
 * phase and interface registries. Pass its `PhaseId` and `InterfaceId` values
 * to geometry, workset, and operator layers instead of repeating name lookup
 * in numerical code.
 *
 * \par Typical use
 * \code{.cpp}
 * const rift::InterfaceCompatibilityCheck accept_pair =
 *     [](const rift::PhaseDescriptor &minus,
 *        const rift::PhaseDescriptor &plus,
 *        const rift::InterfaceSpecification &interface) {
 *         if (interface.operator_key.value() != "finite-rate")
 *             return std::optional<std::string>{"unsupported interface law"};
 *         if (minus.physics_key.value() == plus.physics_key.value())
 *             return std::optional<std::string>{"expected different phase physics for each phase"};
 *         return std::optional<std::string>{};
 *     };
 *
 * auto result = rift::make_phase_graph(
 *     {{"gas", "compressible"}, {"liquid", "low-mach"}},
 *     {{"surface", "liquid", "gas", "finite-rate"}},
 *     accept_pair);
 *
 * if (!result) {
 *     for (const rift::PhaseGraphError &error : result.error())
 *         std::cerr << error.message << '\n';
 *     return;
 * }
 *
 * const rift::PhaseGraph &graph = *result;
 * const rift::PhaseId gas = graph.find_phase("gas").value();
 * const rift::InterfaceId surface =
 *     graph.find_interface("surface").value();
 *
 * const rift::PhaseDescriptor &gas_phase = graph.phase(gas);
 * const rift::InterfaceDescriptor &edge =
 *     graph.material_interface(surface);
 *
 * for (const rift::PhaseDescriptor &phase : graph.phases())
 *     std::cout << phase.id.value() << ": " << phase.name << '\n';
 * \endcode
 *
 * \par Important behavior
 * Numeric identifiers are assigned by lexicographically sorting unique names,
 * so equivalent configurations receive the same IDs regardless of declaration
 * order. Interface orientation always follows the declared minus/plus order.
 * The graph owns its descriptors and is immutable after construction.
 * Geometrical occupancy may change without changing graph identities.
 *
 * \par Failure handling
 * `PhaseGraph` has no public unchecked constructor. `make_phase_graph()`
 * returns all independently detectable configuration errors. Checked name
 * lookup returns `std::nullopt`; numeric lookup throws `std::out_of_range` for
 * an invalid ID.
 * \ingroup phase_graph
 */
class PhaseGraph {
public:
    /**
     * \brief Iterate over every configured phase in stable ID order.
     *
     * Use this view during setup, diagnostics, or bulk initialization:
     *
     * \code{.cpp}
     * for (const rift::PhaseDescriptor &phase : graph.phases())
     *     std::cout << phase.name << '\n';
     * \endcode
     *
     * The returned span borrows graph-owned storage and remains valid until the
     * graph is destroyed.
     *
     * \return immutable span whose index equals each descriptor's `PhaseId`.
     */
    [[nodiscard]] std::span<const PhaseDescriptor> phases() const noexcept { return phases_; }

    /**
     * \brief Iterate over every configured interface in stable ID order.
     *
     * \code{.cpp}
     * for (const rift::InterfaceDescriptor &edge : graph.interfaces()) {
     *     const auto &minus = graph.phase(edge.minus_phase);
     *     const auto &plus = graph.phase(edge.plus_phase);
     * }
     * \endcode
     *
     * The returned span borrows graph-owned storage and preserves each edge's
     * physical minus/plus orientation.
     *
     * \return immutable span whose index equals each descriptor's `InterfaceId`.
     */
    [[nodiscard]] std::span<const InterfaceDescriptor> interfaces() const noexcept { return interfaces_; }

    /**
     * \brief Resolve a previously obtained phase ID in constant time.
     *
     * Use `find_phase()` during configuration, retain the returned ID, and use
     * this indexed lookup in later setup or execution code:
     *
     * \code{.cpp}
     * const rift::PhaseId gas = graph.find_phase("gas").value();
     * const rift::PhaseDescriptor &phase = graph.phase(gas);
     * \endcode
     *
     * \param id phase identity to resolve.
     * \return immutable descriptor associated with `id`.
     * \throws std::out_of_range if `id` is not present.
     */
    [[nodiscard]] const PhaseDescriptor& phase(PhaseId id) const;

    /**
     * \brief Resolve a previously obtained interface ID in constant time.
     *
     * \code{.cpp}
     * const rift::InterfaceId surface =
     *     graph.find_interface("surface").value();
     * const rift::InterfaceDescriptor &edge =
     *     graph.material_interface(surface);
     * \endcode
     *
     * \param id interface identity to resolve.
     * \return immutable descriptor associated with `id`.
     * \throws std::out_of_range if `id` is not present.
     */
    [[nodiscard]] const InterfaceDescriptor& material_interface(InterfaceId id) const;

    /**
     * \brief Translate a configuration phase name into its stable numeric ID.
     *
     * \code{.cpp}
     * if (const auto gas = graph.find_phase("gas"))
     *     std::cout << graph.phase(*gas).physics_key.value() << '\n';
     * \endcode
     *
     * Prefer this checked lookup at configuration boundaries. Retain the result
     * rather than repeating name lookup in hot paths.
     *
     * \param name configuration name to resolve.
     * \return matching phase identity, or no value when the name is absent.
     */
    [[nodiscard]] std::optional<PhaseId> find_phase(std::string_view name) const noexcept;

    /**
     * \brief Translate a configuration interface name into its stable numeric ID.
     *
     * \code{.cpp}
     * if (const auto surface = graph.find_interface("surface")) {
     *     const auto &edge = graph.material_interface(*surface);
     *     std::cout << edge.operator_key.value() << '\n';
     * }
     * \endcode
     *
     * Prefer this checked lookup during setup and retain the resulting ID.
     *
     * \param name configuration name to resolve.
     * \return matching interface identity, or no value when the name is absent.
     */
    [[nodiscard]] std::optional<InterfaceId> find_interface(std::string_view name) const noexcept;

    /**
     * \brief Find whether two phases are joined by the graph's sole pairwise edge.
     *
     * \code{.cpp}
     * const auto gas = graph.find_phase("gas").value();
     * const auto liquid = graph.find_phase("liquid").value();
     * if (const auto edge = graph.find_interface(gas, liquid)) {
     *     const auto &oriented = graph.material_interface(*edge);
     *     const auto &minus = graph.phase(oriented.minus_phase);
     *     const auto &plus = graph.phase(oriented.plus_phase);
     * }
     * \endcode
     *
     * The input pair is unordered. The returned descriptor still retains its
     * declared minus/plus orientation.
     *
     * \param first identity of either incident phase.
     * \param second identity of the other incident phase.
     * \return matching interface identity, or no value when the pair is not adjacent.
     */
    [[nodiscard]] std::optional<InterfaceId> find_interface(PhaseId first, PhaseId second) const noexcept;

    /**
     * \brief Produce a deterministic text identity for diagnostics and metadata.
     *
     * \code{.cpp}
     * const std::string graph_identity = graph.canonical_json();
     * std::cout << graph_identity << '\n';
     * \endcode
     *
     * The JSON is output-only. It records graph names, keys, IDs, and
     * orientation; it is not a solver input format and contains no geometry or
     * solution state.
     *
     * \return canonical compact JSON with schema name and version.
     */
    [[nodiscard]] std::string canonical_json() const;

private:
    /**
     * \brief Permit the validation implementation to invoke the private constructor.
     */
    friend struct detail::PhaseGraphFactory;

    /**
     * \brief Construct a graph from already validated canonical descriptors.
     *
     * \param phases phase descriptors in ascending `PhaseId` order.
     * \param interfaces interface descriptors in ascending `InterfaceId` order.
     */
    PhaseGraph(std::vector<PhaseDescriptor> phases, std::vector<InterfaceDescriptor> interfaces);

    /**
     * \brief Phase descriptors indexed directly by `PhaseId::value()`.
     */
    std::vector<PhaseDescriptor> phases_;

    /**
     * \brief Interface descriptors indexed directly by `InterfaceId::value()`.
     */
    std::vector<InterfaceDescriptor> interfaces_;
};

/**
 * \brief Handle either a validated graph or all errors found while building it.
 *
 * Inspect the result before accessing the graph:
 *
 * \code{.cpp}
 * rift::PhaseGraphResult result =
 *     rift::make_phase_graph({{"gas", "compressible"}}, {});
 * if (result)
 *     std::cout << result->canonical_json() << '\n';
 * else
 *     for (const auto &error : result.error())
 *         std::cerr << error.message << '\n';
 * \endcode
 *
 * \ingroup phase_graph
 */
using PhaseGraphResult = std::expected<PhaseGraph, PhaseGraphErrors>;

/**
 * \brief Turn name-based phase configuration into a validated immutable graph.
 *
 * \par When to use
 * Call this once during run initialization, after configuration parsing and
 * registry creation but before distributing degrees of freedom. Keep the
 * returned graph as the stable topology authority for later subsystems.
 *
 * \par Typical use
 * \code{.cpp}
 * const rift::InterfaceCompatibilityCheck compatibility =
 *     [](const rift::PhaseDescriptor &minus,
 *        const rift::PhaseDescriptor &plus,
 *        const rift::InterfaceSpecification &interface) {
 *         const bool supported =
 *             minus.physics_key.value() == "low-mach" &&
 *             plus.physics_key.value() == "compressible" &&
 *             interface.operator_key.value() == "finite-rate";
 *         return supported
 *             ? std::optional<std::string>{}
 *             : std::optional<std::string>{"compiled pairing is unavailable"};
 *     };
 *
 * rift::PhaseGraphResult result = rift::make_phase_graph(
 *     {{"gas", "compressible"}, {"liquid", "low-mach"}},
 *     {{"surface", "liquid", "gas", "finite-rate"}},
 *     compatibility);
 *
 * if (!result) {
 *     for (const rift::PhaseGraphError &error : result.error())
 *         std::cerr << error.message << '\n';
 *     return;
 * }
 *
 * const rift::PhaseGraph &graph = *result;
 * const rift::PhaseId liquid = graph.find_phase("liquid").value();
 * const rift::InterfaceId surface =
 *     graph.find_interface("surface").value();
 * \endcode
 *
 * \par Important behavior
 * Names are sorted before IDs are assigned, so insertion order does not affect
 * graph identity. The compatibility callback sees resolved phase descriptors
 * in declared minus/plus order. Construction owns its input vectors and moves
 * canonicalized values into the result.
 *
 * \par Failure handling
 * All independently detectable structural and compatibility errors are
 * returned together. A compatibility callback is optional only when
 * `interface_specifications` is empty.
 *
 * \param phase_specifications named phase configurations to validate and resolve.
 * \param interface_specifications oriented pairwise interfaces to validate and resolve.
 * \param compatibility_check cold-path registry callback for ordered interface compatibility.
 * \return validated graph, or all independently detectable construction errors.
 * \ingroup phase_graph
 */
[[nodiscard]] PhaseGraphResult make_phase_graph(std::vector<PhaseSpecification> phase_specifications,
                                                std::vector<InterfaceSpecification> interface_specifications,
                                                InterfaceCompatibilityCheck compatibility_check = {});

} // namespace rift
