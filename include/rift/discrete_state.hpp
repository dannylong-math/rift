#pragma once

/**
 * \file
 * \brief Phase-local deal.II spaces and centrally versioned discrete state.
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deal.II/base/index_set.h>
#include <deal.II/base/mpi.h>
#include <deal.II/dofs/dof_handler.h>
#include <deal.II/dofs/dof_tools.h>
#include <deal.II/fe/fe_nothing.h>
#include <deal.II/fe/fe_q.h>
#include <deal.II/fe/fe_system.h>
#include <deal.II/grid/cell_id.h>
#include <deal.II/grid/tria.h>
#include <deal.II/hp/fe_collection.h>
#include <deal.II/lac/affine_constraints.h>
#include <deal.II/lac/la_parallel_vector.h>
#include <exception>
#include <expected>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/strong_id.hpp>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace rift {

template<int dim> class FieldGroupSpace;
template<int dim> class LevelSetFieldSpace;
template<int dim> class SpaceDraft;
template<int dim> class SpaceSnapshot;
template<int dim> class SpaceRegistry;

/**
 * \defgroup discrete_state Discrete state and spaces
 * \brief Phase-local finite-element spaces and centrally owned state snapshots.
 */

namespace detail {

/** \brief Distinguish field-group identifiers from other strong identifiers. */
struct FieldGroupIdTag {};

/** \brief Distinguish regional-entry identifiers from other strong identifiers. */
struct RegionalEntryIdTag {};

/** \brief Distinguish space epochs from state and snapshot identities. */
struct SpaceEpochTag {};

/** \brief Distinguish space-registry identities from layout epochs. */
struct SpaceRegistryIdTag {};

/** \brief Distinguish immutable state snapshots from accepted state epochs. */
struct StateSnapshotIdTag {};

/** \brief Distinguish publication authorities from snapshot identities. */
struct StateStoreIdTag {};

/** \brief Distinguish collective trials from stores and snapshots. */
struct StateTransactionIdTag {};

/** \brief Distinguish accepted state revisions from private snapshot identities. */
struct StateEpochTag {};

/** \brief Distinguish level-set field revisions from complete state snapshots. */
struct LevelSetFieldSetSnapshotIdTag {};

/** \brief Store the implementation of one phase-local or level-set space. */
template<int dim> struct FieldGroupSpaceData;

/** \brief Store the field-only contents of a provisional space draft. */
template<int dim> struct SpaceDraftData;

/** \brief Store the complete immutable contents of a finalized space snapshot. */
template<int dim> struct SpaceSnapshotData;

/** \brief Give focused private tests controlled access to draft construction seams. */
template<int dim> struct SpaceRegistryAccess;

/** \brief Private post-agreement operation injected by fatal-boundary tests. */
using SpacePostAgreementHook = void (*)();

/** \brief Perform no work in normal registry construction. */
inline void no_space_post_agreement_hook() {}

/**
 * \brief Invoke the fatal handler retained by a run-owned mesh snapshot.
 * \param control retained run communicator and injectable fatal handler.
 * \param status MPI-compatible failure status.
 */
[[noreturn]] void abort_space_operation(const std::shared_ptr<const RunConfigurationControl>& control, int status);

/** \brief Store immutable vectors and version stamps shared by state snapshots. */
struct StateSnapshotData;

/** \brief Store transaction-private vectors until a trial is sealed or abandoned. */
struct MutableStateData;

/** \brief Retain immutable communicator and provenance state for snapshots and tombstones. */
struct StateCollectiveContext;

/** \brief Own the mutable publication registry shared by one movable store handle. */
struct StateStoreAuthority;

/** \brief Retain collective identity after a transaction becomes inactive. */
struct StateTransactionTombstone;

/** \brief Give the state implementation controlled access to layout lifetime state. */
struct StateLayoutAccess;

/** \brief Give focused tests access to exact post-agreement state failure seams. */
struct StateStoreAccess;

/** \brief Private post-agreement operation injected by fatal-boundary tests. */
using StatePostAgreementHook = void (*)();

/** \brief Private regional broadcast operation injected by fatal-boundary tests. */
using StateRegionalBroadcast = int (*)(void*, int, MPI_Datatype, int, MPI_Comm);

/** \brief Private regional staging allocation injected by fatal-boundary tests. */
using StateRegionalStagingFactory = std::vector<std::uint64_t> (*)(std::size_t);

/**
 * \brief Allocate zero-initialized regional synchronization staging.
 * \param size number of regional scalar representations to stage.
 * \return temporary exact-bit buffer committed only after recoverable seal checks.
 */
inline std::vector<std::uint64_t> make_state_regional_staging(const std::size_t size)
{
    return std::vector<std::uint64_t>(size);
}

/** \brief Perform no work at normal state post-agreement boundaries. */
inline void no_state_post_agreement_hook() {}

} // namespace detail

/** \brief Stable identity of one phase-local or full-background field group. */
using FieldGroupId = StrongId<detail::FieldGroupIdTag>;

/** \brief Stable identity of one finalized regional scalar entry. */
using RegionalEntryId = StrongId<detail::RegionalEntryIdTag>;

/** \brief Identity of one immutable finite-element layout generation. */
using SpaceEpoch = StrongId<detail::SpaceEpochTag, std::uint64_t>;

/** \brief Identity of one registry that can finalize only its own drafts. */
using SpaceRegistryId = StrongId<detail::SpaceRegistryIdTag, std::uint64_t>;

/** \brief Unique identity of one accepted or private immutable state snapshot. */
using StateSnapshotId = StrongId<detail::StateSnapshotIdTag, std::uint64_t>;

/** \brief Unique identity of one collective state publication authority. */
using StateStoreId = StrongId<detail::StateStoreIdTag, std::uint64_t>;

/** \brief Unique identity of one collective mutable trial. */
using StateTransactionId = StrongId<detail::StateTransactionIdTag, std::uint64_t>;

/** \brief Identity of one revision published as live accepted state. */
using StateEpoch = StrongId<detail::StateEpochTag, std::uint64_t>;

/** \brief Identity of the level-set field values used by geometry. */
using LevelSetFieldSetSnapshotId = StrongId<detail::LevelSetFieldSetSnapshotIdTag, std::uint64_t>;

/**
 * \brief Configure explicit private-snapshot retention for one state store.
 *
 * \code{.cpp}
 * rift::StateTransitionResult<rift::StateStore>
 * make_unpinned_store(const rift::StateLayout& layout) {
 *     const rift::StateRetentionPolicy policy{
 *         .max_pinned_private_snapshots = 0,
 *     };
 *     return rift::make_state_store(layout, policy);
 * }
 * \endcode
 *
 * Capacity zero is valid: sealing and direct publication still work, while a
 * new pin returns `pin_limit_reached`.
 * \ingroup discrete_state
 */
struct StateRetentionPolicy {
    /** \brief Maximum number of explicitly pinned private snapshots. */
    std::size_t max_pinned_private_snapshots{0};

    /** \brief Compare the complete replicated policy. */
    friend bool operator==(const StateRetentionPolicy&, const StateRetentionPolicy&) = default;
};

/** \brief Classify a deterministic collective or checked-local state failure. */
enum class StateTransitionErrorCode : std::uint8_t {
    /** Ranks entered different collective operations. */
    operation_mismatch,
    /** Ranks supplied different store identities. */
    store_mismatch,
    /** Ranks supplied different transaction identities. */
    transaction_mismatch,
    /** Ranks supplied different snapshot arguments. */
    argument_mismatch,
    /** Factory layouts differ across the retained run communicator. */
    replicated_layout_mismatch,
    /** Factory retention policies differ across ranks. */
    retention_policy_mismatch,
    /** The mutable store authority no longer exists. */
    expired_store,
    /** The transaction was sealed, abandoned, or moved from. */
    inactive_transaction,
    /** The requested snapshot is not retained by the store. */
    unknown_snapshot,
    /** The requested publication target is not a private candidate. */
    wrong_candidate_state,
    /** The requested discard target is not a discardable private candidate. */
    invalid_discard,
    /** The store already retains the configured number of pinned private snapshots. */
    pin_limit_reached,
    /** The candidate descends from an older accepted root. */
    stale_accepted_root,
    /** A finite communicator-consistent identity sequence is exhausted. */
    identity_exhausted,
    /** A local field reference belongs to another space or phase. */
    field_reference_mismatch,
    /** A local field identity does not exist in this layout. */
    unknown_field,
    /** A regional scalar identity does not exist in this layout. */
    unknown_regional_entry,
};

/**
 * \brief Return one fixed-size allocation-free state failure.
 *
 * \code{.cpp}
 * std::string_view begin_error(rift::StateStore& store,
 *                              const rift::StateSnapshotId base) {
 *     const auto result = store.begin_trial_collective(base);
 *     return result ? std::string_view{} : result.error().message();
 * }
 * \endcode
 *
 * The code alone stores the result; `message()` returns static text and never
 * allocates. Equal errors are therefore byte-independent and deterministic.
 * \ingroup discrete_state
 */
struct StateTransitionError {
    /** \brief Machine-readable failure in fixed precedence order. */
    StateTransitionErrorCode code;

    /** \brief Return static diagnostic text for `code`. */
    [[nodiscard]] std::string_view message() const noexcept;

    /** \brief Compare fixed-size error values. */
    friend bool operator==(const StateTransitionError&, const StateTransitionError&) = default;
};

/** \brief Collective state result whose error path does not allocate. */
template<class T> using StateTransitionResult = std::expected<T, StateTransitionError>;

/** \brief Native distributed vector owned by Rift state snapshots. */
using DistributedStateVector = dealii::LinearAlgebra::distributed::Vector<double>;

/** \brief Stable owner-local set of active cells carrying real phase unknowns. */
using SupportEnvelope = std::set<dealii::CellId>;

/**
 * \brief Bind a finite-element generation to its run, graph, mesh, and registry.
 *
 * \code{.cpp}
 * void verify_provenance(
 *     const rift::SpaceDraft<2> &draft,
 *     const std::shared_ptr<const rift::MeshSnapshot<2>> &mesh,
 *     const rift::PhaseGraph &graph) {
 *     const rift::SpaceProvenance provenance = draft.provenance();
 *     assert(provenance.mesh == mesh->id());
 *     assert(provenance.graph == graph.provenance().graph);
 * }
 * \endcode
 *
 * Consumers compare the complete value before reusing cached indices. A bare
 * `SpaceEpoch` is intentionally insufficient provenance.
 * \ingroup discrete_state
 */
struct SpaceProvenance {
    /** \brief Run that owns all collective operations for this space. */
    RunConfigurationId run;
    /** \brief Exact graph instance supplying phase references. */
    PhaseGraphInstanceId graph;
    /** \brief Immutable mesh snapshot numbered by the spaces. */
    MeshSnapshotId mesh;
    /** \brief Registry authorized to finalize the draft. */
    SpaceRegistryId registry;
    /** \brief Never-reused communicator-consistent generation. */
    SpaceEpoch epoch;

    /** \brief Compare every provenance component. */
    friend bool operator==(const SpaceProvenance& left, const SpaceProvenance& right)
    {
        return std::tie(left.run, left.graph, left.mesh, left.registry, left.epoch) ==
               std::tie(right.run, right.graph, right.mesh, right.registry, right.epoch);
    }
};

/**
 * \brief Identify one field only within its complete immutable space provenance.
 *
 * \code{.cpp}
 * std::size_t owned_field_size(const rift::StateLayout& layout,
 *                              const rift::StateSnapshot& snapshot,
 *                              const rift::FieldGroupId group) {
 *     const auto reference = layout.field_reference(group).value();
 *     return snapshot.field(reference).value().get().locally_owned_size();
 * }
 * \endcode
 * \ingroup discrete_state
 */
struct StateFieldReference {
    /** \brief Complete finite-space identity used to interpret vector indices. */
    SpaceProvenance space;
    /** \brief Owning phase, absent only for the level-set field. */
    std::optional<PhaseReference> phase;
    /** \brief Field-group identity within `space`. */
    FieldGroupId group;

    /** \brief Compare provenance, phase, and group together. */
    friend bool operator==(const StateFieldReference& left, const StateFieldReference& right);
};

namespace detail {

/** \brief Select the store component of an atomic state identity reservation. */
inline constexpr std::uint64_t state_store_identity_mask = 1U << 0U;
/** \brief Select the transaction component of an atomic state identity reservation. */
inline constexpr std::uint64_t state_transaction_identity_mask = 1U << 1U;
/** \brief Select the snapshot component of an atomic state identity reservation. */
inline constexpr std::uint64_t state_snapshot_identity_mask = 1U << 2U;
/** \brief Select the accepted-epoch component of an atomic state identity reservation. */
inline constexpr std::uint64_t state_epoch_identity_mask = 1U << 3U;
/** \brief Select the level-set revision component of an atomic state identity reservation. */
inline constexpr std::uint64_t state_level_set_identity_mask = 1U << 4U;

/**
 * \brief Bundle the two semantically distinct state-sequence reservation arguments.
 *
 * \code{.cpp}
 * const rift::detail::StateSequenceReservation request{
 *     .origin_world_rank = 7,
 *     .mask = rift::detail::state_store_identity_mask};
 * \endcode
 *
 * Maintainers construct this only while holding the process-global sequence
 * lock or in a single-rank externally serialized allocator test.
 */
struct StateSequenceReservation {
    /** \brief World rank encoded in the high word of every selected identity. */
    std::uint32_t origin_world_rank;
    /** \brief Bit mask selecting sequence components to reserve atomically. */
    std::uint64_t mask;
};

/**
 * \brief Reserve selected finite identity sequences with all-or-nothing advancement.
 *
 * \code{.cpp}
 * std::array<std::uint64_t, 5> next{};
 * const auto reservation = rift::detail::reserve_state_sequence_values(
 *     next, {.origin_world_rank = 7,
 *            .mask = rift::detail::state_snapshot_identity_mask |
 *                    rift::detail::state_level_set_identity_mask});
 * assert(reservation.has_value());
 * assert(next[2] == 1 && next[4] == 1);
 * \endcode
 *
 * The production root calls this beneath its process-global allocator lock,
 * then broadcasts the values. Focused tests inject every finite exhaustion
 * boundary without corrupting a real process-global sequence.
 *
 * \param next root-local sequences in store/transaction/snapshot/epoch/level-set order.
 * \param request origin rank and selected identity components.
 * \return encoded values in component order, or `identity_exhausted` with `next` unchanged.
 */
StateTransitionResult<std::array<std::uint64_t, 5>> reserve_state_sequence_values(std::array<std::uint64_t, 5>& next,
                                                                                  StateSequenceReservation request);

/**
 * \brief Pair an MPI operation status with its deterministic fatal translation.
 *
 * \code{.cpp}
 * const rift::detail::StateMpiStatus status{
 *     .operation = MPI_SUCCESS, .fatal = MPI_ERR_OTHER};
 * \endcode
 */
struct StateMpiStatus {
    /** \brief Status returned by the MPI operation. */
    int operation;
    /** \brief Deterministic status passed to the retained fatal handler. */
    int fatal;
};

/**
 * \brief Route a non-success state MPI status through the retained fatal policy.
 * \param control retained run control supplying the fatal handler.
 * \param status operation result and deterministic fatal translation.
 * \return normally only when `status.operation == MPI_SUCCESS`.
 * \par Fatal behavior
 * A non-success operation invokes the retained fatal handler with
 * `status.fatal`; if a production handler unexpectedly returns, Rift
 * terminates rather than rejoining an unsafe collective protocol.
 * \par Maintainer workflow
 * Call immediately after the MPI operation and before allocation, mutation, or
 * another collective. Focused tests inject both success and failure statuses.
 */
void require_state_mpi_success(const std::shared_ptr<const RunConfigurationControl>& control, StateMpiStatus status);

} // namespace detail

/**
 * \brief Describe one phase-local continuous-Galerkin field group to build.
 *
 * \code{.cpp}
 * rift::PhaseFieldGroupSpecification make_flow_field(
 *     const rift::PhaseReference gas) {
 *     return {.phase = gas, .name = "flow",
 *             .components = 5, .polynomial_degree = 2};
 * }
 * \endcode
 *
 * Support is supplied once per represented phase through
 * `PhaseSupportSpecification`; fields of the same phase share its closed mask.
 * \ingroup discrete_state
 */
struct PhaseFieldGroupSpecification {
    /** \brief Phase that owns this field group. */
    PhaseReference phase;
    /** \brief Name unique among field groups of the same phase. */
    std::string name;
    /** \brief Number of finite-element components. */
    unsigned int components;
    /** \brief Uniform polynomial degree used on every supported cell. */
    unsigned int polynomial_degree;
};

/**
 * \brief Request the owner-local support of one represented phase.
 *
 * \code{.cpp}
 * rift::PhaseSupportSpecification make_gas_support(
 *     const rift::PhaseReference gas,
 *     const rift::MeshSnapshotId mesh,
 *     rift::SupportEnvelope owned_gas_cells) {
 *     return {
 *         .phase = gas,
 *         .mesh = mesh,
 *         .locally_owned_requested_cells = std::move(owned_gas_cells),
 *     };
 * }
 * \endcode
 *
 * Every rank supplies exactly one record for every phase represented by the
 * replicated field schema. The owner-local set may be empty.
 * \ingroup discrete_state
 */
struct PhaseSupportSpecification {
    /** \brief Phase whose field groups share this support. */
    PhaseReference phase;
    /** \brief Mesh snapshot on which the cell identities were observed. */
    MeshSnapshotId mesh;
    /** \brief Active cells owned by this rank that request the real element. */
    SupportEnvelope locally_owned_requested_cells;
};

/**
 * \brief Inspect the immutable owner-local least-fixed-point support of a phase.
 *
 * \code{.cpp}
 * void verify_closed_support(const rift::FieldGroupSpace<2> &field) {
 *     const rift::PhaseSupport &support = field.support();
 *     assert(std::includes(
 *         support.final_locally_owned_cells().begin(),
 *         support.final_locally_owned_cells().end(),
 *         support.requested_locally_owned_cells().begin(),
 *         support.requested_locally_owned_cells().end()));
 * }
 * \endcode
 * \ingroup discrete_state
 */
class PhaseSupport {
public:
    /** \brief Return the graph-checked phase identity. */
    [[nodiscard]] PhaseReference phase() const noexcept { return phase_; }
    /** \brief Return the mesh whose owner cells form these masks. */
    [[nodiscard]] MeshSnapshotId mesh_id() const noexcept { return mesh_; }
    /** \brief Return this rank's original owner-local request. */
    [[nodiscard]] const SupportEnvelope& requested_locally_owned_cells() const noexcept { return requested_; }
    /** \brief Return owner cells added only by hanging-face closure. */
    [[nodiscard]] const SupportEnvelope& closure_added_locally_owned_cells() const noexcept { return added_; }
    /** \brief Return the closed owner-local mask used for FE assignment. */
    [[nodiscard]] const SupportEnvelope& final_locally_owned_cells() const noexcept { return final_; }

private:
    template<int> friend class SpaceRegistry;

    /** \brief Allocate immutable support while keeping construction private. */
    [[nodiscard]] static std::shared_ptr<const PhaseSupport> create(PhaseReference phase, MeshSnapshotId mesh,
                                                                    SupportEnvelope requested, SupportEnvelope added,
                                                                    SupportEnvelope final_mask)
    {
        struct SharedPhaseSupport final : PhaseSupport {
            SharedPhaseSupport(PhaseReference phase_value, MeshSnapshotId mesh_value, SupportEnvelope requested_value,
                               SupportEnvelope added_value, SupportEnvelope final_value) :
                PhaseSupport(phase_value, mesh_value, std::move(requested_value), std::move(added_value),
                             std::move(final_value))
            {
            }
        };
        return std::make_shared<SharedPhaseSupport>(phase, mesh, std::move(requested), std::move(added),
                                                    std::move(final_mask));
    }

    /** \brief Construct one validated immutable support result. */
    PhaseSupport(PhaseReference phase, MeshSnapshotId mesh, SupportEnvelope requested, SupportEnvelope added,
                 SupportEnvelope final_mask) :
        phase_(phase),
        mesh_(mesh),
        requested_(std::move(requested)),
        added_(std::move(added)),
        final_(std::move(final_mask))
    {
    }

    /** \brief Phase shared by every field group using this object. */
    PhaseReference phase_;
    /** \brief Immutable mesh identity underlying every cell ID. */
    MeshSnapshotId mesh_;
    /** \brief Owner-local input mask. */
    SupportEnvelope requested_;
    /** \brief Owner-local closure delta. */
    SupportEnvelope added_;
    /** \brief Owner-local fixed-point mask. */
    SupportEnvelope final_;
};

namespace detail {

/** \brief Map one represented numeric phase ID to its checked graph reference. */
using RepresentedPhaseMap = std::map<std::uint32_t, PhaseReference>;

/** \brief Group rank-local support declarations by represented numeric phase ID. */
using SupportsByPhase = std::map<std::uint32_t, std::vector<const PhaseSupportSpecification*>>;

/** \brief Share one immutable closed support object among every field of a phase. */
using ClosedPhaseSupports = std::map<std::uint32_t, std::shared_ptr<const PhaseSupport>>;

} // namespace detail

/**
 * \brief Describe the single full-background level-set field group.
 *
 * \code{.cpp}
 * rift::LevelSetFieldGroupSpecification make_level_sets() {
 *     return {.name = "level_sets", .components = 2,
 *             .polynomial_degree = 1};
 * }
 * \endcode
 *
 * Unlike phase fields, this group has no support envelope because every
 * active background cell receives the real finite element.
 * \ingroup discrete_state
 */
struct LevelSetFieldGroupSpecification {
    /** \brief Diagnostic name of the full-background field group. */
    std::string name;
    /** \brief Number of configured level-set components. */
    unsigned int components;
    /** \brief Uniform polynomial degree on the background mesh. */
    unsigned int polynomial_degree;
};

/**
 * \brief Bundle phase-local and geometry-field requirements for one space draft.
 *
 * \code{.cpp}
 * rift::SpaceSpecification make_space_schema(
 *     const rift::PhaseReference gas) {
 *     return {
 *         .phase_fields = {{.phase = gas, .name = "flow",
 *                           .components = 5, .polynomial_degree = 2}},
 *         .level_set = {.name = "level_sets", .components = 1,
 *                       .polynomial_degree = 1},
 *     };
 * }
 * \endcode
 * \ingroup discrete_state
 */
struct SpaceSpecification {
    /** \brief Phase-local field groups requested by compiled phase schemas. */
    std::vector<PhaseFieldGroupSpecification> phase_fields;
    /** \brief The one required full-background level-set field group. */
    LevelSetFieldGroupSpecification level_set;
};

/**
 * \brief Add one global scalar unknown or compatibility row during finalization.
 *
 * \code{.cpp}
 * std::vector<rift::RegionalEntrySpecification> make_regional_schema() {
 *     return {{"closed_region_pressure"}, {"pressure_compatibility"}};
 * }
 * \endcode
 *
 * Policy code determines which entries exist. The discrete-state layer only
 * gives them stable layout identities and central vector ownership.
 * \ingroup discrete_state
 */
struct RegionalEntrySpecification {
    /** \brief Name unique within the finalized regional schema. */
    std::string name;
};

/** \brief Classify a field-schema or final-layout construction failure. */
enum class SpaceBuildErrorCode : std::uint8_t {
    /** A phase field names a phase absent from the supplied graph. */
    unknown_phase,
    /** A phase reference belongs to another graph or run. */
    phase_reference_mismatch,
    /** The graph and retained mesh belong to different runs. */
    graph_provenance_mismatch,
    /** A support record names another immutable mesh snapshot. */
    support_mesh_mismatch,
    /** Rank-local field schemas are not canonically identical. */
    replicated_schema_mismatch,
    /** A represented phase has no rank-local support record. */
    missing_phase_support,
    /** A represented phase has more than one rank-local support record. */
    duplicate_phase_support,
    /** A support record names a phase absent from the field schema. */
    unused_phase_support,
    /** A support request names an active cell owned by another rank. */
    nonowned_support_cell,
    /** A support request names a refined-away or otherwise inactive cell. */
    inactive_support_cell,
    /** Hanging-face closure did not reach its monotone fixed point. */
    closure_nonconvergence,
    /** The finite communicator-consistent epoch space is exhausted. */
    space_id_exhausted,
    /** A registry was asked to finalize another registry's draft. */
    foreign_registry_draft,
    /** A registry was asked to finalize a moved-from or already finalized draft. */
    inactive_draft,
    /** A phase-local field group has no diagnostic/configuration name. */
    empty_field_name,
    /** Two groups owned by the same phase use the same name. */
    duplicate_field_name,
    /** A phase-local field group requests no finite-element components. */
    zero_components,
    /** A phase-local field group requests polynomial degree zero. */
    zero_polynomial_degree,
    /** A support request cannot be resolved as any cell of the immutable mesh. */
    unknown_support_cell,
    /** The required level-set field group has no name. */
    empty_level_set_name,
    /** The required level-set field group requests no components. */
    zero_level_set_components,
    /** The required level-set field group requests polynomial degree zero. */
    zero_level_set_polynomial_degree,
    /** A regional scalar entry has no name. */
    empty_regional_entry_name,
    /** Two regional scalar entries use the same name. */
    duplicate_regional_entry_name,
};

/**
 * \brief Report one invalid space or regional-layout specification.
 *
 * \code{.cpp}
 * void print_space_errors(const rift::SpaceBuildErrors &errors) {
 *     for (const rift::SpaceBuildError &error : errors)
 *         std::cerr << error.message << '\n';
 * }
 * \endcode
 * \ingroup discrete_state
 */
struct SpaceBuildError {
    /** \brief Machine-readable error classification. */
    SpaceBuildErrorCode code;
    /** \brief Human-readable explanation naming the invalid entity. */
    std::string message;
    /** \brief Phase provenance associated with the invalid entity, when applicable. */
    std::optional<PhaseReference> phase;
    /** \brief Exact support-cell identity associated with the error, when applicable. */
    std::optional<dealii::CellId> cell;
    /** \brief Rank that reported this collective diagnostic, when applicable. */
    std::optional<int> reporting_rank;
};

/** \brief Collection of independently detected space-build errors. */
using SpaceBuildErrors = std::vector<SpaceBuildError>;

/**
 * \brief Describe one field vector block in a finalized state layout.
 *
 * \code{.cpp}
 * const rift::StateFieldBlock &block = layout.field_blocks().front();
 * std::cout << block.name << ": " << block.locally_owned_dofs.n_elements();
 * \endcode
 * \ingroup discrete_state
 */
struct StateFieldBlock {
    /** \brief Globally stable field-group identity. */
    FieldGroupId id;
    /** \brief Owning phase, or no value for the level-set block. */
    std::optional<PhaseReference> phase;
    /** \brief Diagnostic field-group name. */
    std::string name;
    /** \brief Locally owned portion of the global vector. */
    dealii::IndexSet locally_owned_dofs;
    /** \brief Whether changes to this block create a new level-set snapshot identity. */
    bool is_level_set;
};

/**
 * \brief Describe one rank-zero-owned scalar block in the final state layout.
 *
 * \code{.cpp}
 * for (const rift::RegionalEntry &entry : layout.regional_entries())
 *     std::cout << entry.id.value() << ": " << entry.name << '\n';
 * \endcode
 * \ingroup discrete_state
 */
struct RegionalEntry {
    /** \brief Stable identity in lexicographic name order. */
    RegionalEntryId id;
    /** \brief Diagnostic name supplied by the regional-constraint system. */
    std::string name;
    /** \brief One-entry ownership set, owned by MPI rank zero. */
    dealii::IndexSet locally_owned_entries;
};

/**
 * \brief Inspect the vector partitioning completed by space finalization.
 *
 * \par Typical use
 * \code{.cpp}
 * rift::StateSnapshot make_initial_state(const rift::StateLayout& layout) {
 *     auto store = rift::make_state_store(layout, {}).value();
 *     return store.snapshot(rift::StateSlot::accepted);
 * }
 * \endcode
 *
 * The layout is immutable and carries the communicator and `SpaceEpoch` used
 * to initialize every central state vector.
 * \ingroup discrete_state
 */
class StateLayout {
public:
    /** \brief Return complete run/graph/mesh/registry/epoch provenance. */
    [[nodiscard]] SpaceProvenance provenance() const noexcept { return provenance_; }
    /** \brief Return the finite-element generation described by this layout. */
    [[nodiscard]] SpaceEpoch space_epoch() const noexcept { return space_epoch_; }
    /** \brief Iterate over field blocks in `FieldGroupId` order. */
    [[nodiscard]] std::span<const StateFieldBlock> field_blocks() const noexcept { return field_blocks_; }
    /** \brief Iterate over regional scalar blocks in `RegionalEntryId` order. */
    [[nodiscard]] std::span<const RegionalEntry> regional_entries() const noexcept { return regional_entries_; }
    /** \brief Return the full-background level-set block identity. */
    [[nodiscard]] FieldGroupId level_set_group() const noexcept { return level_set_group_; }
    /** \brief Return the communicator shared by this layout's distributed vectors. */
    [[nodiscard]] MPI_Comm communicator() const noexcept { return communicator_; }
    /**
     * \brief Resolve one field into a complete checked local-access reference.
     * \param group field group in this immutable layout.
     * \return provenance-bearing reference, or `std::nullopt` when `group` is out of range.
     */
    [[nodiscard]] std::optional<StateFieldReference> field_reference(FieldGroupId group) const noexcept
    {
        if (group.value() >= field_blocks_.size()) {
            return std::nullopt;
        }
        const auto& block = field_blocks_.at(group.value());
        return StateFieldReference{.space = provenance_, .phase = block.phase, .group = block.id};
    }

private:
    template<int dim> friend class SpaceRegistry;
    friend struct detail::StateLayoutAccess;

    /** \brief Construct a validated final layout from a completed space draft. */
    StateLayout(SpaceProvenance provenance, std::shared_ptr<const detail::RunConfigurationControl> run_control,
                std::shared_ptr<const void> mesh_lifetime, std::vector<StateFieldBlock> field_blocks,
                std::vector<RegionalEntry> regional_entries, FieldGroupId level_set_group, MPI_Comm communicator) :
        provenance_(provenance),
        space_epoch_(provenance.epoch),
        run_control_(std::move(run_control)),
        mesh_lifetime_(std::move(mesh_lifetime)),
        field_blocks_(std::move(field_blocks)),
        regional_entries_(std::move(regional_entries)),
        level_set_group_(level_set_group),
        communicator_(communicator)
    {
    }

    /** \brief Complete identity required to interpret every partition. */
    SpaceProvenance provenance_;
    /** \brief Epoch shared by every partition in the layout. */
    SpaceEpoch space_epoch_;
    /** \brief Retained run communicator and fatal policy for later state factories. */
    std::shared_ptr<const detail::RunConfigurationControl> run_control_;
    /** \brief Type-erased immutable mesh owner keeping the communicator alive. */
    std::shared_ptr<const void> mesh_lifetime_;
    /** \brief Field vector partitioners indexed by field-group identity. */
    std::vector<StateFieldBlock> field_blocks_;
    /** \brief Rank-zero-owned scalar partitioners indexed by regional identity. */
    std::vector<RegionalEntry> regional_entries_;
    /** \brief Identity of the field block whose values define geometry. */
    FieldGroupId level_set_group_;
    /** \brief Non-owning communicator handle used to initialize vectors. */
    MPI_Comm communicator_;
};

namespace detail {

/**
 * \brief Retain the private collective lifetime carried by a finalized layout.
 *
 * \code{.cpp}
 * const auto control = rift::detail::StateLayoutAccess::run_control(layout);
 * const auto mesh = rift::detail::StateLayoutAccess::mesh_lifetime(layout);
 * assert(control != nullptr);
 * assert(mesh != nullptr);
 * \endcode
 *
 * State construction uses this gateway only after exact factory agreement;
 * application code should use `make_state_store()`.
 */
struct StateLayoutAccess {
    /**
     * \brief Retain the run communicator and fatal handler.
     * \param layout finalized layout whose private collective lifetime is required.
     * \return shared run control that remains valid independently of `layout`.
     */
    [[nodiscard]] static std::shared_ptr<const RunConfigurationControl> run_control(const StateLayout& layout) noexcept
    {
        return layout.run_control_;
    }

    /**
     * \brief Retain the immutable mesh generation behind the layout.
     * \param layout finalized layout whose mesh lifetime is required.
     * \return type-erased shared mesh owner valid independently of `layout`.
     */
    [[nodiscard]] static std::shared_ptr<const void> mesh_lifetime(const StateLayout& layout) noexcept
    {
        return layout.mesh_lifetime_;
    }
};

} // namespace detail

/**
 * \brief Inspect one immutable phase-local field-group space.
 *
 * \par Typical use
 * \code{.cpp}
 * void print_flow_dofs(
 *     const rift::SpaceSnapshot<2> &snapshot,
 *     const rift::PhaseReference gas) {
 *     const auto flow_id = snapshot.find_field(gas, "flow").value();
 *     const auto &flow = snapshot.field_space(gas, flow_id);
 *     std::cout << flow.dof_handler().n_dofs() << '\n';
 * }
 * \endcode
 *
 * The returned deal.II objects borrow the mesh retained by their snapshot and
 * remain valid only while that space generation remains alive.
 * \ingroup discrete_state
 */
template<int dim> class FieldGroupSpace {
public:
    /** \brief Return the owning phase identity. */
    [[nodiscard]] PhaseReference phase() const noexcept;
    /** \brief Return the stable field-group identity. */
    [[nodiscard]] FieldGroupId id() const noexcept;
    /** \brief Return the diagnostic field-group name. */
    [[nodiscard]] std::string_view name() const noexcept;
    /** \brief Return the number of finite-element components. */
    [[nodiscard]] unsigned int components() const noexcept;
    /** \brief Return the uniform polynomial degree on supported cells. */
    [[nodiscard]] unsigned int polynomial_degree() const noexcept;
    /** \brief Return the phase-shared immutable owner-local support result. */
    [[nodiscard]] const PhaseSupport& support() const noexcept;
    /** \brief Return complete run/graph/mesh/registry/epoch provenance. */
    [[nodiscard]] SpaceProvenance provenance() const noexcept;
    /** \brief Return the space epoch shared by this field group. */
    [[nodiscard]] SpaceEpoch epoch() const noexcept;
    /** \brief Borrow the field group's independently owned DoFHandler. */
    [[nodiscard]] const dealii::DoFHandler<dim>& dof_handler() const noexcept;
    /** \brief Borrow the closed hanging-node and continuity constraints. */
    [[nodiscard]] const dealii::AffineConstraints<double>& constraints() const noexcept;

private:
    template<int> friend class SpaceRegistry;
    template<int> friend class SpaceDraft;
    template<int> friend class SpaceSnapshot;

    /** \brief Wrap stable shared implementation storage. */
    explicit FieldGroupSpace(std::shared_ptr<const detail::FieldGroupSpaceData<dim>> data, const PhaseReference phase) :
        data_(std::move(data)), phase_(phase)
    {
    }

    /** \brief Stable storage retaining the mesh, FE collection, and deal.II objects. */
    std::shared_ptr<const detail::FieldGroupSpaceData<dim>> data_;
    /** \brief Required owning phase, separated from level-set optional storage. */
    PhaseReference phase_;
};

/**
 * \brief Inspect the full-background level-set finite-element space.
 *
 * \code{.cpp}
 * void verify_level_set_background(
 *     const rift::SpaceSnapshot<2> &snapshot) {
 *     const auto &level_sets = snapshot.level_set_space();
 *     for (const auto &cell :
 *          level_sets.dof_handler().active_cell_iterators())
 *         assert(cell->get_fe().dofs_per_cell > 0);
 * }
 * \endcode
 *
 * This space never contains `FE_Nothing` and therefore remains defined on the
 * complete background mesh.
 * \ingroup discrete_state
 */
template<int dim> class LevelSetFieldSpace {
public:
    /** \brief Return complete run/graph/mesh/registry/epoch provenance. */
    [[nodiscard]] SpaceProvenance provenance() const noexcept;
    /** \brief Return the stable field-group identity. */
    [[nodiscard]] FieldGroupId id() const noexcept;
    /** \brief Return the diagnostic field-group name. */
    [[nodiscard]] std::string_view name() const noexcept;
    /** \brief Return the number of configured level-set components. */
    [[nodiscard]] unsigned int components() const noexcept;
    /** \brief Return the uniform polynomial degree on every cell. */
    [[nodiscard]] unsigned int polynomial_degree() const noexcept;
    /** \brief Return the space epoch shared by this field group. */
    [[nodiscard]] SpaceEpoch epoch() const noexcept;
    /** \brief Borrow the full-background DoFHandler. */
    [[nodiscard]] const dealii::DoFHandler<dim>& dof_handler() const noexcept;
    /** \brief Borrow the closed hanging-node constraints. */
    [[nodiscard]] const dealii::AffineConstraints<double>& constraints() const noexcept;

private:
    template<int> friend class SpaceRegistry;
    template<int> friend class SpaceDraft;
    template<int> friend class SpaceSnapshot;

    /** \brief Wrap stable shared implementation storage. */
    explicit LevelSetFieldSpace(std::shared_ptr<const detail::FieldGroupSpaceData<dim>> data) : data_(std::move(data))
    {
    }

    /** \brief Stable storage retaining the mesh, FE collection, and deal.II objects. */
    std::shared_ptr<const detail::FieldGroupSpaceData<dim>> data_;
};

/**
 * \brief Hold provisional field-only spaces before regional layout finalization.
 *
 * \code{.cpp}
 * void inspect_draft(const rift::SpaceDraft<2> &draft) {
 *     assert(draft.active());
 *     std::cout << draft.level_set_space().dof_handler().n_dofs() << '\n';
 * }
 * \endcode
 *
 * A draft deliberately exposes no `StateLayout` and cannot initialize a
 * `StateStore`.
 * \ingroup discrete_state
 */
template<int dim> class SpaceDraft {
public:
    /** \brief Transfer a provisional space generation without copying its identity. */
    SpaceDraft(SpaceDraft&&) noexcept = default;
    /** \brief Replace this provisional generation with another move-only draft. */
    SpaceDraft& operator=(SpaceDraft&&) noexcept = default;
    /** \brief Prevent one provisional generation from being finalized twice. */
    SpaceDraft(const SpaceDraft&) = delete;
    /** \brief Prevent assignment from duplicating finalization authority. */
    SpaceDraft& operator=(const SpaceDraft&) = delete;
    /** \brief Release this draft's shared provisional storage. */
    ~SpaceDraft() = default;

    /** \brief Return the unique provisional epoch reserved for this draft. */
    [[nodiscard]] SpaceEpoch epoch() const noexcept;
    /** \brief Report whether this draft still owns finalization authority. */
    [[nodiscard]] bool active() const noexcept;
    /** \brief Return complete run/graph/mesh/registry/epoch provenance. */
    [[nodiscard]] SpaceProvenance provenance() const noexcept;
    /** \brief Iterate over provisional phase-local spaces. */
    [[nodiscard]] std::span<const FieldGroupSpace<dim>> field_spaces() const noexcept;
    /** \brief Borrow the provisional full-background level-set space. */
    [[nodiscard]] const LevelSetFieldSpace<dim>& level_set_space() const noexcept;

private:
    friend class SpaceRegistry<dim>;
    friend struct detail::SpaceRegistryAccess<dim>;

    /** \brief Construct a draft around validated field-only storage. */
    explicit SpaceDraft(std::shared_ptr<const detail::SpaceDraftData<dim>> data) : data_(std::move(data)) {}

    /** \brief Immutable provisional field-only storage. */
    std::shared_ptr<const detail::SpaceDraftData<dim>> data_;
};

/**
 * \brief Publish immutable field spaces and their complete state layout.
 *
 * \par Typical use
 * \code{.cpp}
 * rift::StateSnapshot initial_state(const rift::SpaceSnapshot<2>& snapshot) {
 *     auto store = rift::make_state_store(snapshot.layout(), {}).value();
 *     const auto accepted = store.snapshot(rift::StateSlot::accepted);
 *     assert(accepted.stamp().space.epoch == snapshot.epoch());
 *     return accepted;
 * }
 * \endcode
 *
 * A later rebuild receives a distinct `SpaceEpoch`; existing snapshots and
 * their borrowed views remain valid for their shared immutable lifetime.
 * \ingroup discrete_state
 */
template<int dim> class SpaceSnapshot {
public:
    /** \brief Return this immutable finite-element generation's identity. */
    [[nodiscard]] SpaceEpoch epoch() const noexcept;
    /** \brief Return complete run/graph/mesh/registry/epoch provenance. */
    [[nodiscard]] SpaceProvenance provenance() const noexcept;
    /** \brief Iterate over phase-local spaces in `FieldGroupId` order. */
    [[nodiscard]] std::span<const FieldGroupSpace<dim>> field_spaces() const noexcept;
    /** \brief Borrow the one full-background level-set space. */
    [[nodiscard]] const LevelSetFieldSpace<dim>& level_set_space() const noexcept;
    /** \brief Find a phase-local group by its configuration name. */
    [[nodiscard]] std::optional<FieldGroupId> find_field(PhaseReference phase, std::string_view name) const noexcept;
    /** \brief Resolve and phase-check a field-group identity. */
    [[nodiscard]] const FieldGroupSpace<dim>& field_space(PhaseReference phase, FieldGroupId group) const;
    /** \brief Borrow the complete vector layout finalized for this generation. */
    [[nodiscard]] const StateLayout& layout() const noexcept;

private:
    friend class SpaceRegistry<dim>;

    /** \brief Construct a snapshot around finalized immutable storage. */
    explicit SpaceSnapshot(std::shared_ptr<const detail::SpaceSnapshotData<dim>> data) : data_(std::move(data)) {}

    /** \brief Immutable spaces and finalized state layout. */
    std::shared_ptr<const detail::SpaceSnapshotData<dim>> data_;
};

/** \brief Result of validated field-only space construction. */
template<int dim> using SpaceDraftResult = std::expected<SpaceDraft<dim>, SpaceBuildErrors>;

/** \brief Result of validated regional-layout finalization. */
template<int dim> using SpaceSnapshotResult = std::expected<SpaceSnapshot<dim>, SpaceBuildErrors>;

/**
 * \brief Build phase-local deal.II spaces and reserve never-reused space epochs.
 *
 * \par Typical use
 * \code{.cpp}
 * rift::SpaceSnapshotResult<2> build_spaces(
 *     const std::shared_ptr<const rift::MeshSnapshot<2>> &mesh,
 *     const rift::PhaseGraph &graph,
 *     rift::SpaceSpecification specification,
 *     std::vector<rift::PhaseSupportSpecification> supports) {
 *     const rift::SpaceRegistry<2> registry(mesh);
 *     auto draft = registry.begin_draft(
 *         graph, std::move(specification), std::move(supports));
 *     if (!draft)
 *         return std::unexpected(draft.error());
 *     return registry.finalize(*draft, {});
 * }
 * \endcode
 *
 * The registry retains the immutable mesh snapshot, which keeps both the
 * triangulation and its owned communicator alive. Construction has a hard
 * program precondition that `mesh` is non-null. Rejected drafts still consume
 * their provisional communicator-consistent epoch.
 * \ingroup discrete_state
 */
template<int dim> class SpaceRegistry {
public:
    /**
     * \brief Retain the immutable background mesh used by all generations.
     * \param mesh non-null immutable mesh snapshot; null violates a hard precondition.
     */
    explicit SpaceRegistry(std::shared_ptr<const MeshSnapshot<dim>> mesh);

    /**
     * \brief Collectively validate a schema and construct provisional field spaces.
     * \param graph run-compatible graph owning every phase reference.
     * \param specification replicated field and level-set schema; declaration order is irrelevant.
     * \param supports exactly one owner-local support record per represented phase on every rank.
     * \return active draft, or byte-identical collective logical diagnostics.
     *
     * The operation reserves its never-reused epoch before validation, closes
     * support to a distributed least fixed point, distributes DoFs, and builds
     * constraints. MPI, deal.II, and unsafe asymmetric allocation failures use
     * the retained fatal handler rather than returning a local error.
     */
    [[nodiscard]] SpaceDraftResult<dim> begin_draft(const PhaseGraph& graph, SpaceSpecification specification,
                                                    std::vector<PhaseSupportSpecification> supports) const;

    /**
     * \brief Add regional scalar blocks and transactionally publish a complete immutable layout.
     * \param draft active lvalue draft; logical errors leave it active and unchanged.
     * \param regional_specifications replicated regional-scalar declarations.
     * \return finalized snapshot, or byte-identical collective logical diagnostics.
     *
     * The operation is collective on the retained mesh communicator. MPI,
     * dependency, and post-agreement allocation failures use the retained
     * fatal handler. Success alone consumes `draft`; rvalues cannot bind.
     */
    [[nodiscard]] SpaceSnapshotResult<dim>
    finalize(SpaceDraft<dim>& draft, std::vector<RegionalEntrySpecification> regional_specifications) const;

private:
    friend struct detail::SpaceRegistryAccess<dim>;

    /** \brief Build a draft from an already reserved epoch and bounded closure budget. */
    [[nodiscard]] SpaceDraftResult<dim>
    begin_draft_with_reserved(const PhaseGraph& graph, SpaceSpecification specification,
                              std::vector<PhaseSupportSpecification> supports,
                              std::expected<SpaceEpoch, SpaceBuildError> reserved,
                              std::optional<dealii::types::global_cell_index> closure_iteration_limit,
                              detail::SpacePostAgreementHook post_agreement_hook) const;

    /** \brief Finalize through an injected post-agreement operation used by private fatal tests. */
    [[nodiscard]] SpaceSnapshotResult<dim>
    finalize_with_hook(SpaceDraft<dim>& draft, std::vector<RegionalEntrySpecification> regional_specifications,
                       detail::SpacePostAgreementHook post_agreement_hook) const;

    /** \brief Validate the replicated field schema and collect represented phases. */
    void validate_field_schema(const PhaseGraph& graph, const SpaceSpecification& specification,
                               detail::RepresentedPhaseMap& represented, SpaceBuildErrors& errors,
                               const std::shared_ptr<const detail::RunConfigurationControl>& run_control,
                               MPI_Comm communicator) const;

    /** \brief Validate one support record against graph, mesh, and owner-local activity. */
    void validate_support_record(const PhaseGraph& graph, const PhaseSupportSpecification& support,
                                 const detail::RepresentedPhaseMap& represented,
                                 const SupportEnvelope& locally_owned_active, const SupportEnvelope& globally_active,
                                 const SupportEnvelope& globally_inactive, SpaceBuildErrors& errors) const;

    /** \brief Validate support cardinality and group records by represented phase. */
    [[nodiscard]] detail::SupportsByPhase
    validate_support_schema(const PhaseGraph& graph, const std::vector<PhaseSupportSpecification>& supports,
                            const detail::RepresentedPhaseMap& represented, const SupportEnvelope& locally_owned_active,
                            const SupportEnvelope& globally_active, const SupportEnvelope& globally_inactive,
                            SpaceBuildErrors& errors) const;

    /** \brief Close one phase's owner-local support to a hanging-face fixed point. */
    [[nodiscard]] std::expected<std::shared_ptr<const PhaseSupport>, SpaceBuildError>
    close_phase_support(const PhaseReference& reference, const PhaseSupportSpecification& support,
                        const SupportEnvelope& locally_owned_active,
                        std::optional<dealii::types::global_cell_index> closure_iteration_limit,
                        const std::shared_ptr<const detail::RunConfigurationControl>& run_control,
                        MPI_Comm communicator) const;

    /** \brief Close every represented phase and collectively report nonconvergence. */
    [[nodiscard]] std::expected<detail::ClosedPhaseSupports, SpaceBuildErrors>
    close_phase_supports(const detail::RepresentedPhaseMap& represented,
                         const detail::SupportsByPhase& support_by_phase, const SupportEnvelope& locally_owned_active,
                         std::optional<dealii::types::global_cell_index> closure_iteration_limit,
                         const std::shared_ptr<const detail::RunConfigurationControl>& run_control,
                         MPI_Comm communicator) const;

    /** \brief Assemble deal.II spaces after every logical collective check succeeds. */
    [[nodiscard]] SpaceDraft<dim> assemble_draft(SpaceSpecification specification, SpaceProvenance provenance,
                                                 const detail::ClosedPhaseSupports& closed_supports) const;

    /** \brief Immutable mesh and retained run communicator. */
    std::shared_ptr<const MeshSnapshot<dim>> mesh_;
    /** \brief Communicator-consistent identity used to reject foreign drafts. */
    SpaceRegistryId id_;
};

/**
 * \brief Stamp one immutable state with complete space, store, and revision identity.
 *
 * \code{.cpp}
 * bool is_published(const rift::StateSnapshot& snapshot) {
 *     const rift::StateSnapshotStamp stamp = snapshot.stamp();
 *     return stamp.published_epoch.has_value();
 * }
 * \endcode
 * \ingroup discrete_state
 */
// Strong IDs deliberately have no invalid default; every stamp supplies all required identities.
struct StateSnapshotStamp {
    /**
     * \brief Construct a stamp only when every required identity is available.
     * \param space_value complete finite-element provenance.
     * \param store_value publication authority identity.
     * \param snapshot_value complete vector-bundle identity.
     * \param published_epoch_value accepted revision, or no value for a private candidate.
     *
     * \par Important behavior
     * There is deliberately no default constructor: index zero is a valid identity,
     * so an incomplete stamp cannot be represented by fabricated zero-valued IDs.
     */
    StateSnapshotStamp(SpaceProvenance space_value, const StateStoreId store_value,
                       const StateSnapshotId snapshot_value, std::optional<StateEpoch> published_epoch_value) noexcept :
        space(space_value), store(store_value), snapshot(snapshot_value), published_epoch(published_epoch_value)
    {
    }

    /** \brief Complete finite-element provenance required to interpret vector indices. */
    SpaceProvenance space;
    /** \brief Publication authority that created this immutable bundle. */
    StateStoreId store;
    /** \brief Unique identity of this complete immutable vector bundle. */
    StateSnapshotId snapshot;
    /** \brief Live accepted revision, absent for private nonlinear candidates. */
    std::optional<StateEpoch> published_epoch;
};

/** \brief Name the accepted or immediately previous published state slot. */
enum class StateSlot : std::uint8_t { accepted, previous };

/**
 * \brief Read an immutable centrally owned collection of state vectors.
 *
 * \par Typical use
 * \code{.cpp}
 * double accepted_field_norm(const rift::StateStore& store,
 *                            const rift::StateFieldReference flow_reference) {
 *     const auto accepted = store.snapshot(rift::StateSlot::accepted);
 *     return accepted.field(flow_reference).value().get().l2_norm();
 * }
 * \endcode
 *
 * Copies share immutable storage. Vector references remain valid while any
 * copy of the snapshot retains that storage.
 * \ingroup discrete_state
 */
class StateSnapshot {
public:
    /** \brief Return the identities that make this snapshot cache-safe. */
    [[nodiscard]] StateSnapshotStamp stamp() const noexcept;
    /** \brief Return the geometry-field revision represented by this snapshot. */
    [[nodiscard]] LevelSetFieldSetSnapshotId level_set_snapshot() const noexcept;
    /**
     * \brief Borrow an immutable field only after validating complete provenance.
     * \param reference complete space, optional phase, and group identity.
     * \return read-only vector reference, `field_reference_mismatch` for foreign
     * provenance, or `unknown_field` for an out-of-range local group.
     *
     * The reference remains valid while this snapshot lives. This is local,
     * allocation-free access and performs no collective operation.
     */
    [[nodiscard]] StateTransitionResult<std::reference_wrapper<const DistributedStateVector>>
    field(StateFieldReference reference) const noexcept;
    /**
     * \brief Read one replicated regional scalar with its exact stored representation.
     * \param entry regional scalar identity from this snapshot's finalized layout.
     * \return locally cached scalar value; no communication or allocation occurs.
     * \throws std::out_of_range when `entry` is outside this snapshot's layout.
     *
     * Every rank may call this local getter independently. Signed zero, infinity,
     * and NaN payload bits are reconstructed without numerical normalization.
     */
    [[nodiscard]] double regional_value(RegionalEntryId entry) const;

private:
    friend class StateStore;
    friend struct detail::StateStoreAccess;
    friend class MutableStateTransaction;

    /** \brief Wrap one retained immutable state implementation. */
    explicit StateSnapshot(std::shared_ptr<const detail::StateSnapshotData> data) : data_(std::move(data)) {}

    /** \brief Shared immutable state storage. */
    std::shared_ptr<const detail::StateSnapshotData> data_;
};

class StateStore;

/**
 * \brief Isolate mutable trial vectors until they are sealed or abandoned.
 *
 * \par Typical use
 * \code{.cpp}
 * rift::StateSnapshot make_constant_candidate(
 *     rift::StateStore& store, const rift::StateSnapshot& base,
 *     const rift::StateFieldReference field) {
 *     auto trial = store.begin_trial_collective(base.stamp().snapshot).value();
 *     trial.field(field).value().get() = 1.0;
 *     return trial.seal_collective().value();
 * }
 * \endcode
 *
 * A transaction is move-only. `seal_collective()` creates a new private snapshot;
 * `abandon()` and destruction discard unsealed mutable storage.
 * \ingroup discrete_state
 */
class MutableStateTransaction {
public:
    /** \brief Transfer the sole mutable authority and leave `other` inactive. */
    MutableStateTransaction(MutableStateTransaction&& other) noexcept;
    /** \brief Replace this trial with `other` and leave `other` inactive. */
    MutableStateTransaction& operator=(MutableStateTransaction&& other) noexcept;
    /** \brief Prevent two transactions from sharing mutable state. */
    MutableStateTransaction(const MutableStateTransaction&) = delete;
    /** \brief Prevent assignment from creating a mutable alias. */
    MutableStateTransaction& operator=(const MutableStateTransaction&) = delete;
    /** \brief Discard unsealed private storage without publishing it. */
    ~MutableStateTransaction();

    /**
     * \brief Borrow a mutable owner-only field after validating complete provenance.
     * \param reference complete space, optional phase, and group identity.
     * \return mutable transaction-private vector reference,
     * `field_reference_mismatch`, `unknown_field`, `expired_store`, or
     * `inactive_transaction`.
     *
     * The reference remains valid only while this transaction stays active.
     * This local access never synchronizes ghost values.
     */
    [[nodiscard]] StateTransitionResult<std::reference_wrapper<DistributedStateVector>>
    field(StateFieldReference reference) noexcept;
    /**
     * \brief Collectively replace one transaction-private regional scalar exactly.
     * \param entry regional scalar identity from the transaction's finalized layout.
     * \param value IEEE-754 binary64 representation to preserve bit for bit.
     * \return success, or the first deterministic collective, lifetime, activity,
     * or `unknown_regional_entry` error.
     *
     * Every retained mesh-communicator rank calls in the same externally
     * serialized transition order with the same transaction, entry, and exact
     * value bits. Rejection leaves all mutable state and identities unchanged.
     * Success updates the rank-zero backend and every rank's replicated cache.
     * MPI failure is routed through the retained fatal handler and terminates.
     */
    [[nodiscard]] StateTransitionResult<void> set_regional_value_collective(RegionalEntryId entry, double value);
    /**
     * \brief Freeze the vectors through the common collective transaction protocol.
     * \return immutable private candidate, or the first deterministic descriptor,
     * expired-store, inactive-transaction, or identity-exhaustion error.
     *
     * Every mesh-communicator rank must call in the same externally serialized
     * transition order. Logical failure leaves the transaction active and consumes
     * no identity; success alone consumes the transaction and snapshot/revision IDs.
     * MPI, dependency, or post-agreement allocation failure is fatal.
     */
    [[nodiscard]] StateTransitionResult<StateSnapshot> seal_collective();
    /** \brief Drop mutable storage without changing any retained snapshot. */
    void abandon() noexcept;
    /** \brief Report whether mutable access and sealing remain valid. */
    [[nodiscard]] bool active() const noexcept;
    /** \brief Return the collective identity retained even after completion. */
    [[nodiscard]] StateTransactionId id() const noexcept;

private:
    friend class StateStore;
    friend struct detail::StateStoreAccess;

    /** \brief Start with a collective tombstone and private base copy. */
    MutableStateTransaction(std::shared_ptr<const detail::StateTransactionTombstone> tombstone,
                            std::unique_ptr<detail::MutableStateData> data);

    /** \brief Seal through injected exact-stage fatal-boundary operations. */
    [[nodiscard]] StateTransitionResult<StateSnapshot>
    seal_collective_with_hook(detail::StatePostAgreementHook hook, detail::StateRegionalBroadcast broadcast,
                              detail::StateRegionalStagingFactory staging);

    /** \brief Collective identity retained after move, abandonment, sealing, or expiry. */
    std::shared_ptr<const detail::StateTransactionTombstone> tombstone_;
    /** \brief Transaction-private mutable vectors, absent after completion. */
    std::unique_ptr<detail::MutableStateData> data_;
};

/**
 * \brief Own accepted, previous, and private state snapshots for one layout.
 *
 * \par Typical use
 * \code{.cpp}
 * rift::StateSnapshot publish_constant(
 *     rift::StateStore& store, const rift::StateFieldReference field) {
 *     const auto accepted = store.snapshot(rift::StateSlot::accepted);
 *     auto trial = store.begin_trial_collective(accepted.stamp().snapshot).value();
 *     trial.field(field).value().get() = 1.0;
 *     const auto candidate = trial.seal_collective().value();
 *     return store.publish_collective(candidate.stamp().snapshot).value();
 * }
 * \endcode
 *
 * Publication advances `StateEpoch` without changing `SpaceEpoch`. The store
 * retains at most one unpinned transient private candidate; callers pin any
 * additional private candidates they need to address by ID.
 * \ingroup discrete_state
 */
class StateStore {
public:
    /** \brief Prevent two authorities from owning the same publication history. */
    StateStore(const StateStore&) = delete;
    /** \brief Prevent assignment from duplicating publication authority. */
    StateStore& operator=(const StateStore&) = delete;
    /**
     * \brief Transfer the handle while preserving the shared stable authority.
     *
     * Transactions created before the move remain attached to the authority and
     * can be completed through the moved-to store. The moved-from handle may only
     * be destroyed; calling any other member on it violates its precondition.
     */
    StateStore(StateStore&&) noexcept;
    /** \brief Prevent reassignment from replacing an externally serialized authority. */
    StateStore& operator=(StateStore&&) = delete;
    /**
     * \brief Expire the weak authority observed by outstanding transactions.
     *
     * External snapshots and transaction bases keep immutable storage, mesh,
     * communicator, and tombstone context alive. Destruction performs no collective.
     */
    ~StateStore();

    /**
     * \brief Return the immutable accepted or previous publication slot.
     * \param slot accepted or immediately previous publication.
     * \return independent immutable handle retaining all required lifetimes.
     * \throws std::out_of_range when `previous` does not exist.
     */
    [[nodiscard]] StateSnapshot snapshot(StateSlot slot) const;
    /**
     * \brief Return any retained snapshot by unique identity.
     * \param id snapshot identity owned by this store.
     * \return independent immutable handle retaining all required lifetimes.
     * \throws std::out_of_range when `id` is unknown or no longer retained.
     */
    [[nodiscard]] StateSnapshot snapshot(StateSnapshotId id) const;
    /**
     * \brief Collectively copy a retained base into an isolated mutable transaction.
     * \param base retained snapshot to copy without mutating its vectors.
     * \return active trial, or operation/store/argument mismatch,
     * unknown-snapshot, or identity-exhaustion error.
     *
     * Logical failure changes no store state and consumes no ID. MPI, dependency,
     * or post-agreement allocation failure is fatal. Calls are collective and
     * externally serialized with all other stores on the communicator.
     */
    [[nodiscard]] StateTransitionResult<MutableStateTransaction> begin_trial_collective(StateSnapshotId base);
    /**
     * \brief Collectively publish a retained private candidate.
     * \param candidate sealed private snapshot in this store.
     * \return published view with the same snapshot ID and a new epoch, or the
     * first operation/store/argument, unknown-snapshot, wrong-candidate,
     * stale-root, or identity-exhaustion error.
     *
     * Logical failure preserves accepted/previous/private registration and consumes
     * no ID. Success rotates accepted to previous atomically. MPI, dependency, or
     * post-agreement allocation failure is fatal.
     */
    [[nodiscard]] StateTransitionResult<StateSnapshot> publish_collective(StateSnapshotId candidate);
    /**
     * \brief Collectively retain a private snapshot against transient replacement.
     * \param candidate retained private snapshot in this store.
     * \return success, or the first operation/store/argument, unknown-snapshot,
     * wrong-candidate, or pin-limit error.
     *
     * Pinning the transient snapshot consumes one explicit policy slot and clears
     * the transient slot. Pinning an already pinned snapshot is an idempotent
     * success even when capacity is full. The operation reserves no identities.
     * Every rank in the retained communicator calls this operation in the same
     * externally serialized transition order. MPI failure is routed through the
     * retained fatal handler and terminates rather than returning a logical error.
     */
    [[nodiscard]] StateTransitionResult<void> pin_collective(StateSnapshotId candidate);
    /**
     * \brief Collectively make a pinned private snapshot the current transient.
     * \param candidate retained private snapshot in this store.
     * \return success, or the first operation/store/argument, unknown-snapshot,
     * or wrong-candidate-state error.
     *
     * A pinned candidate immediately replaces and evicts any former transient.
     * Unpinning the current transient is an idempotent success. The operation
     * reserves no identities. Every rank in the retained communicator calls this
     * operation in the same externally serialized transition order. MPI failure
     * is routed through the retained fatal handler and terminates rather than
     * returning a logical error.
     */
    [[nodiscard]] StateTransitionResult<void> unpin_collective(StateSnapshotId candidate);
    /**
     * \brief Collectively stop retaining the transient private snapshot.
     * \param candidate sealed private snapshot in this store.
     * \return success, or the first operation/store/argument, unknown-snapshot,
     * or invalid-discard error.
     *
     * Pinned private, accepted, and previous snapshots return `invalid_discard`.
     * External handles remain valid after successful removal. Logical failure
     * preserves registration; the operation reserves no IDs. MPI failure is fatal.
     */
    [[nodiscard]] StateTransitionResult<void> discard_collective(StateSnapshotId candidate);
    /** \brief Return the immutable layout generation owned by this store. */
    [[nodiscard]] SpaceEpoch space_epoch() const noexcept;
    /** \brief Return complete finite-space provenance for this authority. */
    [[nodiscard]] SpaceProvenance provenance() const noexcept;
    /** \brief Return the communicator-consistent store identity. */
    [[nodiscard]] StateStoreId id() const noexcept;
    /** \brief Return the collectively agreed retention policy. */
    [[nodiscard]] StateRetentionPolicy retention_policy() const noexcept;

private:
    friend class MutableStateTransaction;
    friend struct detail::StateStoreAccess;

    /** \brief Wrap a collectively constructed stable authority. */
    explicit StateStore(std::shared_ptr<detail::StateStoreAuthority> authority);

    /** \brief Begin through an injected exact-stage fatal-boundary hook. */
    [[nodiscard]] StateTransitionResult<MutableStateTransaction>
    begin_trial_collective_with_hook(StateSnapshotId base, detail::StatePostAgreementHook hook);
    /** \brief Publish through an injected exact-stage fatal-boundary hook. */
    [[nodiscard]] StateTransitionResult<StateSnapshot>
    publish_collective_with_hook(StateSnapshotId candidate, detail::StatePostAgreementHook hook);

    /** \brief Shared stable publication authority; moved-from handles are empty. */
    std::shared_ptr<detail::StateStoreAuthority> authority_;
};

/**
 * \brief Collectively construct one zero-initialized state publication authority.
 *
 * \code{.cpp}
 * rift::StateTransitionResult<rift::StateStore>
 * build_state(const rift::StateLayout& layout) {
 *     return rift::make_state_store(
 *         layout,
 *         rift::StateRetentionPolicy{.max_pinned_private_snapshots = 0});
 * }
 * \endcode
 *
 * Every retained-run rank first agrees the complete layout and policy. Only
 * then do ranks enter the authoritative mesh communicator and allocate native
 * owner-partitioned vectors. MPI, dependency, or asymmetric allocation
 * failures invoke the retained fatal handler. Every rank in the call must use
 * layouts from the same retained run and mesh collective context.
 *
 * \param layout finalized immutable vector layout whose private lifetimes are retained.
 * \param retention replicated explicit retention policy recorded by the store.
 * \return movable store authority, or `replicated_layout_mismatch`,
 * `retention_policy_mismatch`, or `identity_exhausted` without a partial store.
 * \ingroup discrete_state
 */
[[nodiscard]] StateTransitionResult<StateStore> make_state_store(const StateLayout& layout,
                                                                 StateRetentionPolicy retention = {});

namespace detail {

/**
 * \brief Exercise exact post-agreement fatal boundaries without changing public behavior.
 *
 * \code{.cpp}
 * void injected_failure() { throw std::bad_alloc{}; }
 * rift::StateTransitionResult<rift::StateStore>
 * exercise_factory_boundary(const rift::StateLayout& layout) {
 *     return rift::detail::StateStoreAccess::make_state_store(
 *         layout, {}, injected_failure);
 * }
 * \endcode
 *
 * Production entry points pass `no_state_post_agreement_hook`. MPI fatal tests
 * pass a rank-local throwing hook and use a stage-armed real-abort wrapper.
 */
struct StateStoreAccess {
    /**
     * \brief Construct a store and invoke `hook` after exact factory agreement.
     * \param layout replicated immutable layout.
     * \param retention replicated retention policy.
     * \param hook injected post-agreement operation; production supplies a no-op.
     * \return the public factory result when `hook` returns.
     * \par Fatal behavior
     * Any exception from `hook` is routed to the retained fatal handler before
     * ranks enter another collective or release collective resources.
     */
    [[nodiscard]] static StateTransitionResult<StateStore>
    make_state_store(const StateLayout& layout, StateRetentionPolicy retention, StatePostAgreementHook hook);
    /**
     * \brief Begin a trial and invoke `hook` after its first descriptor agreement.
     * \param store externally serialized authority joining the descriptor operation.
     * \param base retained base snapshot argument.
     * \param hook injected post-agreement operation; production supplies a no-op.
     * \return the public begin result when `hook` returns.
     * \par Fatal behavior
     * Any hook exception is fatal before allocation or a later collective.
     */
    [[nodiscard]] static StateTransitionResult<MutableStateTransaction>
    begin_trial(StateStore& store, StateSnapshotId base, StatePostAgreementHook hook);
    /**
     * \brief Seal a trial and invoke `hook` after its first descriptor agreement.
     * \param transaction active trial joining the collective descriptor operation.
     * \param hook injected post-agreement operation; production supplies a no-op.
     * \return the public seal result when `hook` returns.
     * \par Fatal behavior
     * Any hook exception is fatal while the original transaction remains unconsumed.
     */
    [[nodiscard]] static StateTransitionResult<StateSnapshot> seal(MutableStateTransaction& transaction,
                                                                   StatePostAgreementHook hook);
    /**
     * \brief Seal using an injected regional synchronization operation.
     * \param transaction active trial joining the collective descriptor operation.
     * \param broadcast injected bitwise broadcast; production supplies `MPI_Bcast`.
     * \return the public seal result when regional synchronization succeeds.
     * \par Fatal behavior
     * A non-success broadcast status is routed to the retained fatal handler after
     * descriptor agreement and before identity reservation or transaction mutation.
     */
    [[nodiscard]] static StateTransitionResult<StateSnapshot>
    seal_with_regional_broadcast(MutableStateTransaction& transaction, StateRegionalBroadcast broadcast);
    /**
     * \brief Seal using an injected regional synchronization staging allocator.
     * \param transaction active trial joining the collective descriptor operation.
     * \param staging injected exact-bit buffer factory; production uses allocation.
     * \return the public seal result when staging and synchronization succeed.
     * \par Fatal behavior
     * Allocation failure is routed to the retained fatal handler after descriptor
     * agreement and before identity reservation or transaction mutation.
     */
    [[nodiscard]] static StateTransitionResult<StateSnapshot>
    seal_with_regional_staging(MutableStateTransaction& transaction, StateRegionalStagingFactory staging);
    /**
     * \brief Publish a candidate and invoke `hook` after its first descriptor agreement.
     * \param store externally serialized authority joining publication.
     * \param candidate sealed private snapshot argument.
     * \param hook injected post-agreement operation; production supplies a no-op.
     * \return the public publication result when `hook` returns.
     * \par Fatal behavior
     * Any hook exception is fatal before accepted/previous state rotates.
     */
    [[nodiscard]] static StateTransitionResult<StateSnapshot> publish(StateStore& store, StateSnapshotId candidate,
                                                                      StatePostAgreementHook hook);
    /**
     * \brief Inspect one immutable snapshot's replicated regional cache in tests.
     * \param snapshot immutable snapshot under test.
     * \param entry known regional entry identity.
     * \return exact cached binary64 representation on the calling rank.
     */
    [[nodiscard]] static std::uint64_t regional_cache_bits(const StateSnapshot& snapshot, RegionalEntryId entry);
    /**
     * \brief Inspect one immutable snapshot's owner-only regional backend in tests.
     * \param snapshot immutable snapshot under test.
     * \param entry known regional entry identity.
     * \return exact backend representation on mesh rank zero, otherwise no value.
     */
    [[nodiscard]] static std::optional<std::uint64_t> regional_backend_bits(const StateSnapshot& snapshot,
                                                                            RegionalEntryId entry);
    /**
     * \brief Inspect one active transaction's replicated regional cache in tests.
     * \param transaction active transaction under test.
     * \param entry known regional entry identity.
     * \return exact cached binary64 representation on the calling rank.
     */
    [[nodiscard]] static std::uint64_t regional_cache_bits(const MutableStateTransaction& transaction,
                                                           RegionalEntryId entry);
    /**
     * \brief Inspect one active transaction's owner-only regional backend in tests.
     * \param transaction active transaction under test.
     * \param entry known regional entry identity.
     * \return exact backend representation on mesh rank zero, otherwise no value.
     */
    [[nodiscard]] static std::optional<std::uint64_t> regional_backend_bits(const MutableStateTransaction& transaction,
                                                                            RegionalEntryId entry);
    /**
     * \brief Replace only an active transaction's owner backend in a synchronization test.
     * \param transaction active transaction whose replicated cache remains untouched.
     * \param entry known regional entry identity.
     * \param bits exact binary64 representation written only by the backend owner.
     * \par Maintainer workflow
     * Use only to prove that sealing broadcasts authoritative backend bits into
     * every replicated cache. Production regional writes never call this seam.
     */
    static void set_regional_backend_bits_for_test(MutableStateTransaction& transaction, RegionalEntryId entry,
                                                   std::uint64_t bits);
    /**
     * \brief Atomically replace the process-local root identity sequences for a focused test.
     * \param replacement store/transaction/snapshot/epoch/level-set next values.
     * \return previous sequence values, which the caller must restore before returning.
     * \par Maintainer workflow
     * Use only in a single-rank, externally serialized exhaustion test. Production
     * identity allocation never calls this seam.
     */
    [[nodiscard]] static std::array<std::uint64_t, 5>
    replace_identity_sequences_for_test(std::array<std::uint64_t, 5> replacement);
};

} // namespace detail

namespace detail {

/**
 * \brief Keep every object needed to inspect one field group alive together.
 *
 * \par When to use
 * `build_field_space()` creates this maintainer-facing aggregate and the public
 * `FieldGroupSpace` or `LevelSetFieldSpace` wrapper shares it read-only. Do not
 * construct it in application code.
 *
 * \par Typical use
 * \code{.cpp}
 * template <int dim>
 * dealii::types::global_dof_index locally_owned_dof_count(
 *     const std::shared_ptr<const
 *         rift::detail::FieldGroupSpaceData<dim>> &data) {
 *     assert(data != nullptr);
 *     return data->dof_handler->locally_owned_dofs().n_elements();
 * }
 * \endcode
 *
 * \par Important behavior
 * Declaration order is intentional: the shared triangulation outlives the
 * `DoFHandler`, and the finite-element collection outlives its use by that
 * handler. Public wrappers expose only const access.
 *
 * \tparam dim spatial dimension of the background triangulation.
 * \ingroup discrete_state
 */
// Identity members intentionally cannot represent an uninitialized sentinel value.
template<int dim> struct FieldGroupSpaceData {
    /**
     * \brief Initialize every required field-space identity and ownership value.
     * \param mesh_value immutable mesh retained by the field space.
     * \param provenance_value complete checked space identity.
     * \param id_value stable field-group index.
     * \param phase_value owning phase, or no value for the level-set group.
     * \param name_value validated diagnostic name.
     * \param components_value positive component count.
     * \param polynomial_degree_value positive polynomial degree.
     * \param support_value closed phase support, or null for the level-set group.
     */
    FieldGroupSpaceData(std::shared_ptr<const MeshSnapshot<dim>> mesh_value, SpaceProvenance provenance_value,
                        const FieldGroupId id_value, std::optional<PhaseReference> phase_value, std::string name_value,
                        const unsigned int components_value, std::shared_ptr<const PhaseSupport> support_value,
                        const unsigned int polynomial_degree_value) :
        mesh(std::move(mesh_value)),
        provenance(provenance_value),
        id(id_value),
        phase(phase_value),
        name(std::move(name_value)),
        components(components_value),
        polynomial_degree(polynomial_degree_value),
        support(std::move(support_value))
    {
    }

    /** \brief Immutable mesh whose lifetime encloses the attached DoFHandler. */
    std::shared_ptr<const MeshSnapshot<dim>> mesh;
    /** \brief Complete checked space identity. */
    SpaceProvenance provenance;
    /** \brief Stable index used by the finalized state layout. */
    FieldGroupId id;
    /** \brief Owning phase, absent only for the full-background level-set group. */
    std::optional<PhaseReference> phase;
    /** \brief Diagnostic field-group name retained from validated configuration. */
    std::string name;
    /** \brief Number of scalar components represented by each real element. */
    unsigned int components;
    /** \brief Uniform degree used by every real element in this group. */
    unsigned int polynomial_degree;
    /** \brief Shared phase support, absent only for the level-set group. */
    std::shared_ptr<const PhaseSupport> support;
    /** \brief Real element and, for partial support, non-dominating `FE_Nothing`. */
    dealii::hp::FECollection<dim> finite_elements;
    /** \brief Independently numbered degrees of freedom for this field group. */
    std::unique_ptr<dealii::DoFHandler<dim>> dof_handler;
    /** \brief Closed hanging-node and continuity constraints for this numbering. */
    dealii::AffineConstraints<double> constraints;
};

/**
 * \brief Retain validated field spaces between construction and finalization.
 *
 * \par When to use
 * `SpaceRegistry::begin_draft()` creates this data after field validation.
 * Geometry and transfer code may borrow the public `SpaceDraft` wrapper while
 * regional constraint code determines the final scalar schema.
 *
 * \par Typical use
 * \code{.cpp}
 * template <int dim>
 * std::size_t draft_field_count(
 *     const std::shared_ptr<const rift::detail::SpaceDraftData<dim>> &data) {
 *     assert(data != nullptr);
 *     assert(data->mesh != nullptr);
 *     assert(data->level_set_space.provenance() == data->provenance);
 *     return data->field_spaces.size();
 * }
 * \endcode
 *
 * \par Important behavior
 * This aggregate deliberately contains no `StateLayout`; constructing a
 * central state store before regional entries are known is therefore
 * impossible through the public API.
 *
 * \tparam dim spatial dimension of every retained field space.
 * \ingroup discrete_state
 */
// The reserved epoch intentionally has no invalid default value.
template<int dim> struct SpaceDraftData {
    /**
     * \brief Initialize one complete validated draft generation.
     * \param mesh_value immutable mesh retained by the draft.
     * \param provenance_value complete reserved identity.
     * \param field_spaces_value phase-local spaces in stable order.
     * \param level_set_space_value full-background level-set space.
     */
    SpaceDraftData(std::shared_ptr<const MeshSnapshot<dim>> mesh_value, SpaceProvenance provenance_value,
                   std::vector<FieldGroupSpace<dim>> field_spaces_value,
                   LevelSetFieldSpace<dim> level_set_space_value) :
        mesh(std::move(mesh_value)),
        provenance(provenance_value),
        field_spaces(std::move(field_spaces_value)),
        level_set_space(std::move(level_set_space_value))
    {
    }

    /** \brief Immutable mesh retained for the complete draft lifetime. */
    std::shared_ptr<const MeshSnapshot<dim>> mesh;
    /** \brief Complete identity reserved before validation begins. */
    SpaceProvenance provenance;
    /** \brief Phase-local groups in stable `FieldGroupId` order. */
    std::vector<FieldGroupSpace<dim>> field_spaces;
    /** \brief One field group defined over the entire background mesh. */
    LevelSetFieldSpace<dim> level_set_space;
};

/**
 * \brief Keep a finalized space generation and its vector layout immutable.
 *
 * \par When to use
 * `SpaceRegistry::finalize()` promotes draft data into this aggregate only
 * after regional scalar validation succeeds. Public code accesses it through
 * `SpaceSnapshot`.
 *
 * \par Typical use
 * \code{.cpp}
 * template <int dim>
 * const rift::StateLayout &checked_layout(
 *     const std::shared_ptr<const
 *         rift::detail::SpaceSnapshotData<dim>> &data) {
 *     assert(data != nullptr);
 *     assert(data->mesh != nullptr);
 *     assert(data->layout.space_epoch() == data->provenance.epoch);
 *     return data->layout;
 * }
 * \endcode
 *
 * \par Important behavior
 * The layout and all spaces carry the same `SpaceEpoch`. Shared ownership of
 * this aggregate makes borrowed deal.II objects stable for the snapshot's
 * lifetime.
 *
 * \tparam dim spatial dimension of the finalized generation.
 * \ingroup discrete_state
 */
// The finalized epoch intentionally has no invalid default value.
template<int dim> struct SpaceSnapshotData {
    /**
     * \brief Initialize one complete finalized space generation.
     * \param mesh_value immutable mesh retained by the snapshot.
     * \param provenance_value complete finalized identity.
     * \param field_spaces_value finalized phase-local spaces.
     * \param level_set_space_value finalized full-background level-set space.
     * \param layout_value complete field and regional partitioning.
     */
    SpaceSnapshotData(std::shared_ptr<const MeshSnapshot<dim>> mesh_value, SpaceProvenance provenance_value,
                      std::vector<FieldGroupSpace<dim>> field_spaces_value,
                      LevelSetFieldSpace<dim> level_set_space_value, StateLayout layout_value) :
        mesh(std::move(mesh_value)),
        provenance(provenance_value),
        field_spaces(std::move(field_spaces_value)),
        level_set_space(std::move(level_set_space_value)),
        layout(std::move(layout_value))
    {
    }

    /** \brief Mesh retained while any public space snapshot survives. */
    std::shared_ptr<const MeshSnapshot<dim>> mesh;
    /** \brief Immutable provenance shared by spaces and vector partitions. */
    SpaceProvenance provenance;
    /** \brief Finalized phase-local groups in stable identity order. */
    std::vector<FieldGroupSpace<dim>> field_spaces;
    /** \brief Finalized full-background geometry-field group. */
    LevelSetFieldSpace<dim> level_set_space;
    /** \brief Complete field and regional vector partitioning. */
    StateLayout layout;
};

/** \brief MPI adapter used to query rank during space identity allocation. */
using SpaceCommRank = int (*)(MPI_Comm, int*);

/** \brief MPI adapter used to broadcast one space identity sequence. */
using SpaceBroadcast = int (*)(void*, int, MPI_Datatype, int, MPI_Comm);

/** \brief MPI adapter used to query a space communicator's size. */
using SpaceCommSize = int (*)(MPI_Comm, int*);

/**
 * \brief Reserve one communicator-consistent finite space identity.
 *
 * Communicator rank zero consumes its process-local sequence and broadcasts
 * it. The mesh origin occupies the high word, so disjoint registries remain
 * unambiguous within one MPI execution. MPI status failures are fatal; the
 * maximum 32-bit sequence is a logical exhaustion result.
 *
 * \param control retained fatal handler.
 * \param communicator mesh-derived intracommunicator.
 * \param mesh mesh identity supplying the world-origin word.
 * \param next root-local monotonic sequence.
 * \param communicator_rank injectable rank query.
 * \param broadcast injectable sequence broadcast.
 * \return encoded identity value or structured exhaustion error.
 */
inline std::expected<std::uint64_t, SpaceBuildError>
reserve_space_identity(const std::shared_ptr<const RunConfigurationControl>& control, const MPI_Comm communicator,
                       const MeshSnapshotId mesh, std::atomic<std::uint64_t>& next,
                       const SpaceCommRank communicator_rank = MPI_Comm_rank,
                       const SpaceBroadcast broadcast = MPI_Bcast)
{
    int rank = 0;
    if (communicator_rank(communicator, &rank) != MPI_SUCCESS) {
        abort_space_operation(control, MPI_ERR_OTHER);
    }
    std::uint64_t sequence = 0;
    if (rank == 0) {
        sequence = next.fetch_add(1, std::memory_order_relaxed);
    }
    if (broadcast(&sequence, 1, MPI_UINT64_T, 0, communicator) != MPI_SUCCESS) {
        abort_space_operation(control, MPI_ERR_OTHER);
    }
    if (sequence >= std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(SpaceBuildError{.code = SpaceBuildErrorCode::space_id_exhausted,
                                               .message = "the finite space identity sequence is exhausted",
                                               .phase = std::nullopt,
                                               .cell = std::nullopt,
                                               .reporting_rank = std::nullopt});
    }
    const auto origin = static_cast<std::uint32_t>(mesh.value() >> 32U);
    return (static_cast<std::uint64_t>(origin) << 32U) | sequence;
}

/** \brief MPI adapter used to gather one local payload size from every rank. */
using GatherSpaceSizes = int (*)(int, std::span<int>, MPI_Comm);

/**
 * \brief Describe one exact variable-length space-payload gather.
 *
 * Private tests construct this value to exercise count, displacement, and MPI
 * failure handling without passing invalid buffers to an MPI implementation.
 *
 * \code{.cpp}
 * void gather_two_payloads(
 *     const std::shared_ptr<const rift::detail::RunConfigurationControl>
 *         &control,
 *     MPI_Comm communicator) {
 *     std::string local = "rank payload";
 *     std::array<int, 2> sizes{
 *         static_cast<int>(local.size()), static_cast<int>(local.size())};
 *     std::array<int, 2> offsets{0, sizes[0]};
 *     std::vector<char> bytes(
 *         static_cast<std::size_t>(sizes[0] + sizes[1]));
 *     rift::detail::SpacePayloadGatherRequest request{
 *         .local = local,
 *         .sizes = sizes,
 *         .offsets = offsets,
 *         .bytes = bytes,
 *     };
 *     const auto operations = rift::detail::space_collective_operations();
 *     if (operations.gather_bytes(request, communicator) != MPI_SUCCESS)
 *         rift::detail::abort_space_operation(control, MPI_ERR_OTHER);
 * }
 * \endcode
 */
struct SpacePayloadGatherRequest {
    /** \brief Rank-local bytes to publish. */
    std::string_view local;
    /** \brief Byte count supplied by every communicator rank. */
    std::span<const int> sizes;
    /** \brief Destination offset for every communicator rank. */
    std::span<const int> offsets;
    /** \brief Contiguous receive storage for all rank payloads. */
    std::span<char> bytes;
};

/** \brief MPI adapter used to gather exact variable-length payload bytes. */
using GatherSpaceBytes = int (*)(const SpacePayloadGatherRequest&, MPI_Comm);

/**
 * \brief Collect the MPI operations used by discrete-space collectives.
 *
 * Production binds these entries to MPI. Private tests replace one operation
 * at a time and observe the retained fatal handler without corrupting a real
 * communicator or entering an unmatched collective.
 *
 * \code{.cpp}
 * std::vector<int> gather_payload_sizes(
 *     const std::shared_ptr<const rift::detail::RunConfigurationControl>
 *         &control,
 *     MPI_Comm communicator, std::string_view local) {
 *     int size = 0;
 *     const rift::detail::SpaceCollectiveOperations operations =
 *         rift::detail::space_collective_operations();
 *     if (operations.communicator_size(communicator, &size) != MPI_SUCCESS)
 *         rift::detail::abort_space_operation(control, MPI_ERR_OTHER);
 *     std::vector<int> sizes(static_cast<std::size_t>(size));
 *     if (operations.gather_sizes(
 *             static_cast<int>(local.size()), sizes, communicator) !=
 *         MPI_SUCCESS)
 *         rift::detail::abort_space_operation(control, MPI_ERR_OTHER);
 *     return sizes;
 * }
 * \endcode
 *
 * Every participant must invoke the selected operation in the same collective
 * order. A non-success MPI status is not recoverable and is routed to the run's
 * fatal handler by the caller.
 */
struct SpaceCollectiveOperations {
    /** \brief Query communicator size. */
    SpaceCommSize communicator_size;
    /** \brief Gather one integer payload size per rank. */
    GatherSpaceSizes gather_sizes;
    /** \brief Gather the exact payload bytes. */
    GatherSpaceBytes gather_bytes;
};

/**
 * \brief Return production MPI adapters for discrete-space collectives.
 * \return adapters bound to communicator size and exact all-gather operations.
 * \par Failure behavior
 * The adapters return MPI statuses; callers route non-success through
 * `abort_space_operation()` before any rank can advance.
 * \par Maintainer workflow
 * Production callers use this default. Focused private tests replace one entry
 * to exercise failure routing without corrupting an MPI communicator.
 */
inline SpaceCollectiveOperations space_collective_operations() noexcept
{
    return {
        .communicator_size = MPI_Comm_size,
        .gather_sizes =
            +[](const int local, const std::span<int> sizes, const MPI_Comm communicator) {
                return MPI_Allgather(&local, 1, MPI_INT, sizes.data(), 1, MPI_INT, communicator);
            },
        .gather_bytes =
            +[](const SpacePayloadGatherRequest& request, const MPI_Comm communicator) {
                return MPI_Allgatherv(request.local.data(), static_cast<int>(request.local.size()), MPI_CHAR,
                                      request.bytes.data(), request.sizes.data(), request.offsets.data(), MPI_CHAR,
                                      communicator);
            },
    };
}

/** \brief Reserve a registry identity from an injected finite sequence. */
inline SpaceRegistryId
reserve_space_registry_id_with_counter(const std::shared_ptr<const RunConfigurationControl>& control,
                                       MPI_Comm communicator, MeshSnapshotId mesh, std::atomic<std::uint64_t>& next);

/** \brief Reserve a globally unambiguous registry identity on its mesh communicator. */
inline SpaceRegistryId reserve_space_registry_id(const std::shared_ptr<const RunConfigurationControl>& control,
                                                 const MPI_Comm communicator, const MeshSnapshotId mesh)
{
    static std::atomic<std::uint64_t> next{0};
    return reserve_space_registry_id_with_counter(control, communicator, mesh, next);
}

/** \brief Reserve a registry identity from an injected finite sequence. */
inline SpaceRegistryId
reserve_space_registry_id_with_counter(const std::shared_ptr<const RunConfigurationControl>& control,
                                       const MPI_Comm communicator, const MeshSnapshotId mesh,
                                       std::atomic<std::uint64_t>& next)
{
    auto value = reserve_space_identity(control, communicator, mesh, next);
    if (!value) {
        abort_space_operation(control, MPI_ERR_OTHER);
    }
    return SpaceRegistryId::from_index(*value);
}

/** \brief Reserve an epoch from an injected finite sequence. */
inline std::expected<SpaceEpoch, SpaceBuildError>
reserve_space_epoch_with_counter(const std::shared_ptr<const RunConfigurationControl>& control, MPI_Comm communicator,
                                 MeshSnapshotId mesh, std::atomic<std::uint64_t>& next);

/** \brief Reserve an epoch before any logical draft validation. */
inline std::expected<SpaceEpoch, SpaceBuildError>
reserve_space_epoch(const std::shared_ptr<const RunConfigurationControl>& control, const MPI_Comm communicator,
                    const MeshSnapshotId mesh)
{
    static std::atomic<std::uint64_t> next{0};
    return reserve_space_epoch_with_counter(control, communicator, mesh, next);
}

/** \brief Reserve an epoch from an injected finite sequence. */
inline std::expected<SpaceEpoch, SpaceBuildError>
reserve_space_epoch_with_counter(const std::shared_ptr<const RunConfigurationControl>& control,
                                 const MPI_Comm communicator, const MeshSnapshotId mesh,
                                 std::atomic<std::uint64_t>& next)
{
    auto value = reserve_space_identity(control, communicator, mesh, next);
    if (!value) {
        return std::unexpected(value.error());
    }
    return SpaceEpoch::from_index(*value);
}

/** \brief MPI adapter used for the closure changed-flag maximum. */
using SpaceAllreduceMaximum = int (*)(int, int&, MPI_Comm);

/** \brief Agree whether any owner-local support mask grew in one closure step. */
inline int collective_space_changed(
    const std::shared_ptr<const RunConfigurationControl>& control, const MPI_Comm communicator, const int local_changed,
    const SpaceAllreduceMaximum maximum = +[](const int local, int& global, const MPI_Comm comm) {
        return MPI_Allreduce(&local, &global, 1, MPI_INT, MPI_MAX, comm);
    })
{
    int global_changed = 0;
    if (maximum(local_changed, global_changed, communicator) != MPI_SUCCESS) {
        abort_space_operation(control, MPI_ERR_OTHER);
    }
    return global_changed;
}

/**
 * \brief Append one structured diagnostic while validating a space schema.
 *
 * This helper keeps error collection uniform so validation can report all
 * independent input mistakes in one `std::unexpected` value.
 *
 * \code{.cpp}
 * rift::SpaceBuildErrors errors;
 * rift::detail::add_space_error(
 *     errors, rift::SpaceBuildErrorCode::empty_field_name,
 *     "a phase field group has an empty name");
 * \endcode
 *
 * \param errors collection that receives the new diagnostic.
 * \param code machine-readable classification of the invalid input.
 * \param message human-readable diagnostic to move into the collection.
 * \param phase phase provenance associated with the diagnostic, when any.
 * \param cell mesh-cell identity associated with the diagnostic, when any.
 */
inline void add_space_error(SpaceBuildErrors& errors, const SpaceBuildErrorCode code, std::string message,
                            std::optional<PhaseReference> phase = std::nullopt,
                            std::optional<dealii::CellId> cell = std::nullopt)
{
    errors.push_back(
        {.code = code, .message = std::move(message), .phase = phase, .cell = cell, .reporting_rank = std::nullopt});
}

/**
 * \brief Route every rank-local exception at a collective space boundary to the fatal handler.
 *
 * Once a rank begins allocation, deal.II construction, or mesh traversal
 * after collective agreement, its peers may already be entering a later
 * collective. Returning or unwinding independently is therefore unsafe.
 * Public registry operations use this boundary around the complete
 * post-epoch construction sequence; private tests pass throwing operations to
 * verify both fatal classifications without corrupting MPI state.
 *
 * \tparam Operation nullary operation performed inside the fatal boundary.
 * \param control retained run control supplying the fatal handler.
 * \param operation construction or validation operation to invoke.
 * \return exactly the value returned by `operation` on success.
 */
template<class Operation>
decltype(auto) invoke_space_fatal_boundary(const std::shared_ptr<const RunConfigurationControl>& control,
                                           Operation&& operation)
{
    try {
        return std::forward<Operation>(operation)();
    }
    catch (const std::bad_alloc&) {
        abort_space_operation(control, MPI_ERR_NO_MEM);
    }
    catch (...) {
        abort_space_operation(control, MPI_ERR_OTHER);
    }
}

/**
 * \brief Encode arbitrary byte strings with unambiguous decimal length framing.
 * \param values byte strings in the semantic order selected by the caller.
 * \return concatenated length-prefixed records; embedded NUL bytes are retained.
 * \par Failure behavior
 * Allocation failure is handled by the surrounding fatal boundary when used
 * between collectives.
 * \par Maintainer workflow
 * Use the paired `unpack_space_strings()` decoder and add a round-trip test for
 * any new payload schema.
 */
inline std::string pack_space_strings(const std::span<const std::string> values)
{
    std::string result;
    for (const auto& value : values) {
        result += std::to_string(value.size());
        result.push_back(':');
        result += value;
    }
    return result;
}

/**
 * \brief Decode strings produced by `pack_space_strings`.
 * \param packed concatenated decimal-length-framed byte records.
 * \return decoded byte strings in their original order.
 * \throws std::logic_error when framing is missing or truncated.
 * \par Maintainer workflow
 * Call only beneath a fatal boundary for collectively supplied internal data;
 * malformed bytes cannot be converted into a recoverable logical input error.
 */
inline std::vector<std::string> unpack_space_strings(const std::string_view packed)
{
    std::vector<std::string> result;
    std::size_t cursor = 0;
    while (cursor < packed.size()) {
        const auto separator = packed.find(':', cursor);
        if (separator == std::string_view::npos) {
            throw std::logic_error("invalid internal space diagnostic framing");
        }
        const auto count =
            static_cast<std::size_t>(std::stoull(std::string(packed.substr(cursor, separator - cursor))));
        cursor = separator + 1;
        if (count > packed.size() - cursor) {
            throw std::logic_error("truncated internal space diagnostic framing");
        }
        result.emplace_back(packed.substr(cursor, count));
        cursor += count;
    }
    return result;
}

/**
 * \brief Gather one arbitrary byte payload from every rank in communicator order.
 * \param control retained fatal handler for allocation and MPI failures.
 * \param communicator collective mesh/run communicator.
 * \param local rank-local bytes, including any embedded NUL values.
 * \param operations production MPI adapters or focused private-test seams.
 * \return one exact payload per communicator rank.
 * \par Failure behavior
 * Count overflow and MPI failures invoke the fatal handler; allocation failure
 * is handled by the caller's surrounding fatal boundary.
 * \par Maintainer workflow
 * Keep sizes, offsets, and payload gathering in this order so no rank advances
 * after another rank has failed.
 */
inline std::vector<std::string>
all_gather_space_payloads(const std::shared_ptr<const RunConfigurationControl>& control, const MPI_Comm communicator,
                          const std::string_view local,
                          const SpaceCollectiveOperations operations = space_collective_operations())
{
    if (local.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        abort_space_operation(control, MPI_ERR_COUNT);
    }
    int size = 0;
    if (operations.communicator_size(communicator, &size) != MPI_SUCCESS) {
        abort_space_operation(control, MPI_ERR_OTHER);
    }
    const int local_size = static_cast<int>(local.size());
    std::vector<int> sizes;
    std::vector<int> offsets;
    std::vector<char> bytes;
    sizes.resize(static_cast<std::size_t>(size));
    offsets.resize(static_cast<std::size_t>(size));
    if (operations.gather_sizes(local_size, sizes, communicator) != MPI_SUCCESS) {
        abort_space_operation(control, MPI_ERR_OTHER);
    }
    std::size_t total = 0;
    for (int rank = 0; rank < size; ++rank) {
        const int rank_size = sizes.at(static_cast<std::size_t>(rank));
        if (rank_size < 0 ||
            total > static_cast<std::size_t>(std::numeric_limits<int>::max()) - static_cast<std::size_t>(rank_size)) {
            abort_space_operation(control, MPI_ERR_COUNT);
        }
        offsets.at(static_cast<std::size_t>(rank)) = static_cast<int>(total);
        total += static_cast<std::size_t>(rank_size);
    }
    bytes.resize(total);
    if (operations.gather_bytes({.local = local, .sizes = sizes, .offsets = offsets, .bytes = bytes}, communicator) !=
        MPI_SUCCESS) {
        abort_space_operation(control, MPI_ERR_OTHER);
    }
    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(size));
    const std::span<const char> gathered_bytes(bytes);
    for (int rank = 0; rank < size; ++rank) {
        const auto payload =
            gathered_bytes.subspan(static_cast<std::size_t>(offsets.at(static_cast<std::size_t>(rank))),
                                   static_cast<std::size_t>(sizes.at(static_cast<std::size_t>(rank))));
        result.emplace_back(payload.begin(), payload.end());
    }
    return result;
}

/**
 * \brief Produce the canonical replicated schema bytes used for exact rank agreement.
 * \param specification rank-local phase-field and level-set declarations.
 * \return bytewise canonical schema, independent of phase-field declaration order.
 * \par Failure behavior
 * This local operation performs no MPI calls; allocation failure is handled by
 * the surrounding collective fatal boundary.
 * \par Maintainer workflow
 * Include every field that changes finite-element layout and add an independent
 * one-field-at-a-time MPI mismatch oracle when extending the schema.
 */
inline std::string canonical_space_schema(const SpaceSpecification& specification)
{
    std::vector<std::string> fields;
    fields.reserve(specification.phase_fields.size() + 1);
    for (const auto& field : specification.phase_fields) {
        std::vector<std::string> pieces{std::to_string(field.phase.graph.run.value()),
                                        std::to_string(field.phase.graph.graph.value()),
                                        std::to_string(field.phase.phase.value()),
                                        field.name,
                                        std::to_string(field.components),
                                        std::to_string(field.polynomial_degree)};
        fields.push_back(pack_space_strings(pieces));
    }
    std::ranges::sort(fields);
    std::vector<std::string> level{specification.level_set.name, std::to_string(specification.level_set.components),
                                   std::to_string(specification.level_set.polynomial_degree)};
    fields.push_back(pack_space_strings(level));
    return pack_space_strings(fields);
}

/**
 * \brief Internal diagnostic paired with the rank that reported it.
 *
 * Maintainer code preserves phase provenance and exact CellId bytes while
 * sorting and deduplicating diagnostics from every rank.
 *
 * \code{.cpp}
 * bool reported_by_rank_one(const rift::PhaseReference gas) {
 *     const rift::detail::RankedSpaceBuildError error{
 *         .code = rift::SpaceBuildErrorCode::unknown_phase,
 *         .message = "phase is absent from the graph",
 *         .phase = gas,
 *         .cell = std::nullopt,
 *         .rank = 1,
 *     };
 *     const auto ordering_key =
 *         rift::detail::ranked_space_error_key(error);
 *     return std::get<7>(ordering_key) == 1;
 * }
 * \endcode
 */
struct RankedSpaceBuildError {
    /** \brief Machine-readable error classification. */
    SpaceBuildErrorCode code;
    /** \brief Human-readable description. */
    std::string message;
    /** \brief Phase involved in the error, when applicable. */
    std::optional<PhaseReference> phase;
    /** \brief Cell involved in the error, when applicable. */
    std::optional<dealii::CellId> cell;
    /** \brief Communicator rank that reported the error. */
    int rank;
};

/**
 * \brief Encode one local diagnostic for collective byte transport.
 * \param error structured local diagnostic, including optional phase and cell.
 * \return length-framed bytes preserving every diagnostic field exactly.
 * \par Failure behavior
 * Valid diagnostics encode without collectives; allocation failure is caught
 * by the registry's surrounding fatal boundary.
 * \par Maintainer workflow
 * Add new diagnostic fields here and in `decode_space_error()` together, then
 * extend the round-trip and rank-permutation tests.
 */
inline std::string encode_space_error(const SpaceBuildError& error)
{
    const auto phase = error.phase;
    return pack_space_strings(std::array<std::string, 8>{
        std::to_string(static_cast<unsigned int>(error.code)), error.message, phase ? "1" : "0",
        phase ? std::to_string(phase->graph.run.value()) : "", phase ? std::to_string(phase->graph.graph.value()) : "",
        phase ? std::to_string(phase->phase.value()) : "", error.cell ? "1" : "0",
        error.cell ? error.cell->to_string() : ""});
}

/**
 * \brief Decode one transported diagnostic and attach its reporting rank.
 * \param control retained fatal handler for invalid internal transport bytes.
 * \param record one length-framed diagnostic from `encode_space_error()`.
 * \param rank communicator rank that supplied `record`.
 * \return complete maintainer diagnostic carrying the reporting rank.
 * \par Failure behavior
 * Invalid conversion is handled by the surrounding fatal boundary; a wrong
 * field count invokes `abort_space_operation()` directly.
 * \par Maintainer workflow
 * Keep this decoder structurally paired with `encode_space_error()` and verify
 * optional provenance and CellId bytes with round-trip tests.
 */
inline RankedSpaceBuildError decode_space_error(const std::shared_ptr<const RunConfigurationControl>& control,
                                                const std::string& record, const int rank)
{
    const auto pieces = unpack_space_strings(record);
    if (pieces.size() != 8) {
        abort_space_operation(control, MPI_ERR_OTHER);
    }
    std::optional<PhaseReference> phase;
    if (pieces.at(2) == "1") {
        phase = PhaseReference{.graph = {.run = RunConfigurationId::from_index(std::stoull(pieces.at(3))),
                                         .graph = PhaseGraphInstanceId::from_index(std::stoull(pieces.at(4)))},
                               .phase = PhaseId::from_index(static_cast<std::uint32_t>(std::stoul(pieces.at(5))))};
    }
    std::optional<dealii::CellId> cell;
    if (pieces.at(6) == "1") {
        cell.emplace(pieces.at(7));
    }
    return {.code = static_cast<SpaceBuildErrorCode>(std::stoul(pieces.at(0))),
            .message = pieces.at(1),
            .phase = phase,
            .cell = cell,
            .rank = rank};
}

/**
 * \brief Return the canonical ordering and exact-deduplication key for one diagnostic.
 * \param error decoded diagnostic with explicit reporting rank.
 * \return tuple ordered by code, provenance, cell bytes, rank, and message.
 * \par Failure behavior
 * This local projection has no collective; allocation failure from CellId
 * string conversion is handled by the surrounding fatal boundary.
 * \par Maintainer workflow
 * When diagnostic identity changes, update this key and the multi-rank
 * permutation/deduplication oracle together.
 */
inline auto ranked_space_error_key(const RankedSpaceBuildError& error)
{
    return std::tuple(error.code, error.phase.has_value(),
                      error.phase ? error.phase->graph.run.value() : std::uint64_t{},
                      error.phase ? error.phase->graph.graph.value() : std::uint64_t{},
                      error.phase ? error.phase->phase.value() : std::uint32_t{}, error.cell.has_value(),
                      error.cell ? error.cell->to_string() : std::string{}, error.rank, error.message);
}

/**
 * \brief Decode every rank payload into one flat diagnostic collection.
 * \param control retained fatal handler for malformed internal payloads.
 * \param gathered one framed payload in communicator-rank order.
 * \return decoded diagnostics before canonical sorting and deduplication.
 * \par Failure behavior
 * Malformed bytes or rank overflow violate the transport invariant and enter
 * the surrounding fatal boundary.
 * \par Maintainer workflow
 * Preserve communicator order here; canonical ordering belongs in
 * `collective_space_errors()` and has independent permutation tests.
 */
inline std::vector<RankedSpaceBuildError>
decode_space_error_payloads(const std::shared_ptr<const RunConfigurationControl>& control,
                            const std::vector<std::string>& gathered)
{
    std::vector<RankedSpaceBuildError> result;
    for (std::size_t rank = 0; rank < gathered.size(); ++rank) {
        for (const auto& record : unpack_space_strings(gathered.at(rank))) {
            result.push_back(decode_space_error(control, record, static_cast<int>(rank)));
        }
    }
    return result;
}

/**
 * \brief Merge local logical diagnostics into a byte-identical rank-ordered result.
 * \param control retained fatal handler for internal transport failures.
 * \param communicator collective registry communicator.
 * \param local logical diagnostics found by this rank.
 * \param operations production collective adapters or private-test seams.
 * \return canonically ordered, exactly deduplicated diagnostics on every rank.
 * \par Failure behavior
 * MPI, allocation, and malformed internal payload failures are fatal; validated
 * logical diagnostics remain recoverable values.
 * \par Maintainer workflow
 * Preserve the encode/gather/decode/sort/deduplicate order and extend the
 * multi-error permutation oracle whenever diagnostic identity changes.
 */
inline SpaceBuildErrors
collective_space_errors(const std::shared_ptr<const RunConfigurationControl>& control, const MPI_Comm communicator,
                        const SpaceBuildErrors& local,
                        const SpaceCollectiveOperations operations = space_collective_operations())
{
    std::vector<std::string> records;
    records.reserve(local.size());
    for (const auto& error : local) {
        records.push_back(encode_space_error(error));
    }
    const auto gathered = all_gather_space_payloads(control, communicator, pack_space_strings(records), operations);
    auto merged = decode_space_error_payloads(control, gathered);
    std::ranges::sort(merged, [](const auto& left, const auto& right) {
        return ranked_space_error_key(left) < ranked_space_error_key(right);
    });
    const auto duplicate = std::ranges::unique(merged, {}, ranked_space_error_key);
    merged.erase(duplicate.begin(), duplicate.end());
    SpaceBuildErrors result;
    for (const auto& error : merged) {
        result.push_back({.code = error.code,
                          .message = error.message,
                          .phase = error.phase,
                          .cell = error.cell,
                          .reporting_rank = error.rank});
    }
    return result;
}

/** \brief Gather the communicator union of owner-local active-cell identities. */
inline SupportEnvelope collective_cell_union(const std::shared_ptr<const RunConfigurationControl>& control,
                                             const MPI_Comm communicator, const SupportEnvelope& local)
{
    std::vector<std::string> ids;
    ids.reserve(local.size());
    for (const auto& id : local) {
        ids.push_back(id.to_string());
    }
    SupportEnvelope result;
    for (const auto& payload : all_gather_space_payloads(control, communicator, pack_space_strings(ids))) {
        for (const auto& id : unpack_space_strings(payload)) {
            result.emplace(id);
        }
    }
    return result;
}

/**
 * \brief Invoke a cell operation only when its iterator is not artificial.
 * \param artificial result of the iterator's `is_artificial()` query.
 * \param operation deferred operation that may safely access cell metadata.
 *
 * Distributed triangulations forbid several metadata queries on artificial
 * cells. Deferring the complete operation guarantees that the callable is not
 * evaluated for such iterators, while giving focused tests a deterministic
 * oracle for partitions that do not happen to expose an artificial neighbor.
 */
template<typename Operation> void visit_non_artificial_cell(const bool artificial, Operation&& operation)
{
    if (!artificial) {
        std::forward<Operation>(operation)();
    }
}

/**
 * \brief Verify one synchronized active-FE index without touching artificial cells.
 * \tparam Reader deferred nullary active-index query.
 * \param control retained fatal policy for a dependency invariant failure.
 * \param artificial whether deal.II classifies the cell as artificial.
 * \param expected owner-derived active FE index expected after synchronization.
 * \param reader query invoked only for owner or ghost cells.
 *
 * A mismatch means deal.II failed to publish the owner-selected hp index to a
 * locally visible cell. Continuing would assemble a different discrete space
 * on different ranks, so the dependency invariant uses the fatal boundary.
 */
template<typename Reader>
void verify_synchronized_active_fe_index(const std::shared_ptr<const RunConfigurationControl>& control,
                                         const bool artificial, const unsigned int expected, Reader&& reader)
{
    visit_non_artificial_cell(artificial, [&] {
        if (std::forward<Reader>(reader)() != expected) {
            abort_space_operation(control, MPI_ERR_OTHER);
        }
    });
}

/**
 * \brief Find owner-return requests induced by active hanging interfaces.
 * \tparam dim spatial dimension of the immutable triangulation.
 * \param triangulation background mesh whose non-artificial active cells are scanned.
 * \param global_mask communicator union of the current owner-local masks.
 * \return cell IDs on every touched hanging interface visible to this rank.
 */
template<int dim>
SupportEnvelope hanging_face_additions(const dealii::Triangulation<dim>& triangulation,
                                       const SupportEnvelope& global_mask)
{
    SupportEnvelope requested_additions;
    for (const auto& cell : triangulation.active_cell_iterators()) {
        if (cell->is_artificial()) {
            continue;
        }
        for (unsigned int face = 0; face < dealii::GeometryInfo<dim>::faces_per_cell; ++face) {
            if (cell->at_boundary(face) || !cell->neighbor_is_coarser(face)) {
                continue;
            }
            const auto [coarse_face, subface] = cell->neighbor_of_coarser_neighbor(face);
            static_cast<void>(subface);
            const auto coarse = cell->neighbor(face);
            visit_non_artificial_cell(coarse->is_artificial(), [&] {
                SupportEnvelope interface_cells{coarse->id()};
                for (unsigned int child = 0; child < dealii::GeometryInfo<dim>::max_children_per_face; ++child) {
                    const auto fine = coarse->neighbor_child_on_subface(coarse_face, child);
                    visit_non_artificial_cell(fine->is_artificial(), [&] { interface_cells.insert(fine->id()); });
                }
                if (std::ranges::any_of(interface_cells, [&](const auto& id) { return global_mask.contains(id); })) {
                    requested_additions.insert(interface_cells.begin(), interface_cells.end());
                }
            });
        }
    }
    return requested_additions;
}

/**
 * \brief Validate and collectively agree one regional-scalar schema.
 * \param control retained fatal-MPI policy and run communicator.
 * \param communicator communicator shared by the registry snapshot.
 * \param specifications rank-local regional declarations.
 * \return local diagnostics ready for the caller's combined collective merge.
 */
inline SpaceBuildErrors validate_regional_schema(const std::shared_ptr<const RunConfigurationControl>& control,
                                                 const MPI_Comm communicator,
                                                 const std::vector<RegionalEntrySpecification>& specifications)
{
    SpaceBuildErrors errors;
    std::map<std::string, std::size_t, std::less<>> occurrences;
    std::vector<std::string> canonical_regions;
    canonical_regions.reserve(specifications.size());
    for (const auto& entry : specifications) {
        if (entry.name.empty()) {
            add_space_error(errors, SpaceBuildErrorCode::empty_regional_entry_name,
                            "a regional entry has an empty name");
        }
        ++occurrences[entry.name];
        canonical_regions.push_back(entry.name);
    }
    for (const auto& [name, count] : occurrences) {
        if (count > 1) {
            add_space_error(errors, SpaceBuildErrorCode::duplicate_regional_entry_name,
                            "regional entry '" + name + "' is declared more than once");
        }
    }
    std::ranges::sort(canonical_regions);
    const auto schemas = all_gather_space_payloads(control, communicator, pack_space_strings(canonical_regions));
    if (!std::ranges::all_of(schemas, [&](const auto& schema) { return schema == schemas.front(); })) {
        add_space_error(errors, SpaceBuildErrorCode::replicated_schema_mismatch,
                        "the replicated regional-entry schema differs between ranks");
    }
    return errors;
}

/**
 * \brief Build and number one independent deal.II field-group space.
 *
 * \par When to use
 * `SpaceRegistry` calls this only after validating names, component counts,
 * degrees, phases, and support-cell identities. Application code uses the
 * resulting public wrappers instead.
 *
 * \par Typical use
 * \code{.cpp}
 * const auto data = rift::detail::build_field_space<2>(
 *     mesh, provenance, rift::FieldGroupId::from_index(0), gas,
 *     "flow", 4, 1, support, false);
 * assert(data->dof_handler->n_dofs() > 0);
 * \endcode
 *
 * \par Important behavior
 * Scalar groups use `FE_Q` directly. Multi-component groups use `FESystem`.
 * A phase-local group adds component-compatible, non-dominating `FE_Nothing`
 * outside its envelope; a full-background group contains only its real
 * element. The returned data owns a closed constraint matrix.
 *
 * \tparam dim spatial dimension of the triangulation.
 * \param mesh immutable mesh snapshot retained with the DoFHandler.
 * \param provenance complete checked identity of the surrounding draft.
 * \param id stable field-group identity assigned by the registry.
 * \param phase owning phase, or no value for the level-set group.
 * \param name validated diagnostic/configuration name.
 * \param components positive component count.
 * \param polynomial_degree positive uniform polynomial degree.
 * \param support phase-shared closed support, or null for the background group.
 * \param full_background whether every cell must use the real element.
 * \return shared immutable implementation storage for a public space wrapper.
 */
// The string and support envelope are ownership sinks moved into the immutable space data.
template<int dim>
std::shared_ptr<const FieldGroupSpaceData<dim>>
build_field_space(const std::shared_ptr<const MeshSnapshot<dim>>& mesh, const SpaceProvenance& provenance,
                  const FieldGroupId id, const std::optional<PhaseReference>& phase, std::string&& name,
                  const unsigned int components, const unsigned int polynomial_degree,
                  const std::shared_ptr<const PhaseSupport>& support, const bool full_background)
{
    const auto run_control = MeshSnapshotAccess<dim>::run_control(*mesh);
    SupportEnvelope global_support;
    if (!full_background) {
        global_support = collective_cell_union(run_control, mesh->communicator(), support->final_locally_owned_cells());
    }
    auto data = std::shared_ptr<FieldGroupSpaceData<dim>>(new FieldGroupSpaceData<dim>(
        mesh, provenance, id, phase, std::move(name), components, support, polynomial_degree));
    if (components == 1) {
        data->finite_elements.push_back(dealii::FE_Q<dim>(polynomial_degree));
        if (!full_background) {
            data->finite_elements.push_back(dealii::FE_Nothing<dim>(1, false));
        }
    }
    else {
        data->finite_elements.push_back(dealii::FESystem<dim>(dealii::FE_Q<dim>(polynomial_degree), components));
        if (!full_background) {
            data->finite_elements.push_back(dealii::FESystem<dim>(dealii::FE_Nothing<dim>(1, false), components));
        }
    }

    data->dof_handler = std::make_unique<dealii::DoFHandler<dim>>(mesh->triangulation());
    if (!full_background) {
        for (const auto& cell : data->dof_handler->active_cell_iterators()) {
            if (cell->is_locally_owned()) {
                cell->set_active_fe_index(data->support->final_locally_owned_cells().contains(cell->id()) ? 0 : 1);
            }
        }
    }

    if (full_background) {
        // Construction always inserts the real element before reaching this full-background branch.
        data->dof_handler->distribute_dofs(*data->finite_elements.begin());
    }
    else {
        data->dof_handler->distribute_dofs(data->finite_elements);
    }
    for (const auto& cell : data->dof_handler->active_cell_iterators()) {
        const auto expected = static_cast<unsigned int>(full_background || global_support.contains(cell->id()) ? 0 : 1);
        verify_synchronized_active_fe_index(run_control, cell->is_artificial(), expected,
                                            [&] { return cell->active_fe_index(); });
    }
    const auto locally_relevant = dealii::DoFTools::extract_locally_relevant_dofs(*data->dof_handler);
    data->constraints.reinit(data->dof_handler->locally_owned_dofs(), locally_relevant);
    if (data->dof_handler->has_active_dofs()) {
        dealii::DoFTools::make_hanging_node_constraints(*data->dof_handler, data->constraints);
    }
    data->constraints.close();
    return data;
}

} // namespace detail

namespace detail {

/**
 * \brief Exercise bounded draft-construction decisions without mutating public registry state.
 *
 * Maintainer tests inject an exhaustion result or a deliberately short
 * closure budget. Production always reserves through the communicator and
 * uses the mathematically sufficient active-cell bound.
 *
 * \tparam dim mesh dimension handled by the registry.
 *
 * \code{.cpp}
 * template <int dim>
 * rift::SpaceDraftResult<dim> build_with_one_iteration(
 *     const rift::SpaceRegistry<dim> &registry,
 *     const rift::PhaseGraph &graph,
 *     rift::SpaceSpecification specification,
 *     std::vector<rift::PhaseSupportSpecification> supports) {
 *     return rift::detail::SpaceRegistryAccess<dim>::begin_draft(
 *         registry, graph, std::move(specification), std::move(supports),
 *         rift::SpaceEpoch::from_index(7), 1);
 * }
 *
 * template <int dim>
 * rift::SpaceSnapshotResult<dim> finalize_after_agreement(
 *     const rift::SpaceRegistry<dim> &registry,
 *     rift::SpaceDraft<dim> &draft,
 *     rift::detail::SpacePostAgreementHook hook) {
 *     return rift::detail::SpaceRegistryAccess<dim>::finalize(
 *         registry, draft, {}, hook);
 * }
 * \endcode
 *
 * These helpers remain collective on the registry communicator. Tests must
 * give every rank the same reserved epoch and collective call sequence. The
 * post-agreement hook models an unsafe rank-local dependency or allocation
 * failure; any exception is routed through the retained fatal handler.
 */
template<int dim> struct SpaceRegistryAccess {
    /**
     * \brief Invoke the private post-reservation draft path.
     * \param registry registry under test.
     * \param graph replicated phase graph.
     * \param specification replicated field schema.
     * \param supports owner-local support records.
     * \param reserved injected communicator-consistent epoch or exhaustion error.
     * \param closure_iteration_limit optional test-only fixed-point budget.
     * \param post_agreement_hook injected unsafe operation, normally a no-op.
     * \return active draft or collective logical diagnostics.
     */
    [[nodiscard]] static SpaceDraftResult<dim>
    begin_draft(const SpaceRegistry<dim>& registry, const PhaseGraph& graph, SpaceSpecification specification,
                std::vector<PhaseSupportSpecification> supports, std::expected<SpaceEpoch, SpaceBuildError> reserved,
                std::optional<dealii::types::global_cell_index> closure_iteration_limit = std::nullopt,
                const SpacePostAgreementHook post_agreement_hook = no_space_post_agreement_hook)
    {
        return registry.begin_draft_with_reserved(graph, std::move(specification), std::move(supports),
                                                  std::move(reserved), closure_iteration_limit, post_agreement_hook);
    }

    /**
     * \brief Invoke transactional finalization with a private post-agreement hook.
     * \param registry registry under test.
     * \param draft active non-const lvalue retained on logical failure.
     * \param regional_specifications replicated regional schema.
     * \param post_agreement_hook injected unsafe operation after collective validation.
     * \return finalized snapshot or collective logical diagnostics.
     */
    [[nodiscard]] static SpaceSnapshotResult<dim>
    finalize(const SpaceRegistry<dim>& registry, SpaceDraft<dim>& draft,
             std::vector<RegionalEntrySpecification> regional_specifications,
             const SpacePostAgreementHook post_agreement_hook)
    {
        return registry.finalize_with_hook(draft, std::move(regional_specifications), post_agreement_hook);
    }

    /**
     * \brief Rebind only draft provenance to exercise finalization invariants.
     * \param draft active draft whose field storage is retained.
     * \param provenance replacement identity used by the focused test.
     * \return active draft sharing the original field storage under replacement provenance.
     */
    [[nodiscard]] static SpaceDraft<dim> with_provenance(SpaceDraft<dim> draft, const SpaceProvenance provenance)
    {
        auto data = std::make_shared<SpaceDraftData<dim>>(draft.data_->mesh, provenance, draft.data_->field_spaces,
                                                          draft.data_->level_set_space);
        return SpaceDraft<dim>(std::move(data));
    }
};

} // namespace detail

template<int dim> PhaseReference FieldGroupSpace<dim>::phase() const noexcept { return phase_; }
template<int dim> FieldGroupId FieldGroupSpace<dim>::id() const noexcept { return data_->id; }
template<int dim> std::string_view FieldGroupSpace<dim>::name() const noexcept { return data_->name; }
template<int dim> unsigned int FieldGroupSpace<dim>::components() const noexcept { return data_->components; }
template<int dim> unsigned int FieldGroupSpace<dim>::polynomial_degree() const noexcept
{
    return data_->polynomial_degree;
}
template<int dim> const PhaseSupport& FieldGroupSpace<dim>::support() const noexcept { return *data_->support; }
template<int dim> SpaceProvenance FieldGroupSpace<dim>::provenance() const noexcept { return data_->provenance; }
template<int dim> SpaceEpoch FieldGroupSpace<dim>::epoch() const noexcept { return data_->provenance.epoch; }
template<int dim> const dealii::DoFHandler<dim>& FieldGroupSpace<dim>::dof_handler() const noexcept
{
    return *data_->dof_handler;
}
template<int dim> const dealii::AffineConstraints<double>& FieldGroupSpace<dim>::constraints() const noexcept
{
    return data_->constraints;
}

template<int dim> FieldGroupId LevelSetFieldSpace<dim>::id() const noexcept { return data_->id; }
template<int dim> SpaceProvenance LevelSetFieldSpace<dim>::provenance() const noexcept { return data_->provenance; }
template<int dim> std::string_view LevelSetFieldSpace<dim>::name() const noexcept { return data_->name; }
template<int dim> unsigned int LevelSetFieldSpace<dim>::components() const noexcept { return data_->components; }
template<int dim> unsigned int LevelSetFieldSpace<dim>::polynomial_degree() const noexcept
{
    return data_->polynomial_degree;
}
template<int dim> SpaceEpoch LevelSetFieldSpace<dim>::epoch() const noexcept { return data_->provenance.epoch; }
template<int dim> const dealii::DoFHandler<dim>& LevelSetFieldSpace<dim>::dof_handler() const noexcept
{
    return *data_->dof_handler;
}
template<int dim> const dealii::AffineConstraints<double>& LevelSetFieldSpace<dim>::constraints() const noexcept
{
    return data_->constraints;
}

template<int dim> SpaceEpoch SpaceDraft<dim>::epoch() const noexcept { return data_->provenance.epoch; }
template<int dim> bool SpaceDraft<dim>::active() const noexcept { return data_ != nullptr; }
template<int dim> SpaceProvenance SpaceDraft<dim>::provenance() const noexcept { return data_->provenance; }
template<int dim> std::span<const FieldGroupSpace<dim>> SpaceDraft<dim>::field_spaces() const noexcept
{
    return data_->field_spaces;
}
template<int dim> const LevelSetFieldSpace<dim>& SpaceDraft<dim>::level_set_space() const noexcept
{
    return data_->level_set_space;
}

template<int dim> SpaceEpoch SpaceSnapshot<dim>::epoch() const noexcept { return data_->provenance.epoch; }
template<int dim> SpaceProvenance SpaceSnapshot<dim>::provenance() const noexcept { return data_->provenance; }
template<int dim> std::span<const FieldGroupSpace<dim>> SpaceSnapshot<dim>::field_spaces() const noexcept
{
    return data_->field_spaces;
}
template<int dim> const LevelSetFieldSpace<dim>& SpaceSnapshot<dim>::level_set_space() const noexcept
{
    return data_->level_set_space;
}
template<int dim>
std::optional<FieldGroupId> SpaceSnapshot<dim>::find_field(const PhaseReference phase,
                                                           const std::string_view name) const noexcept
{
    for (const auto& field : data_->field_spaces) {
        const auto candidate = field.phase();
        if (candidate.graph.run == phase.graph.run && candidate.graph.graph == phase.graph.graph &&
            candidate.phase == phase.phase && field.name() == name) {
            return field.id();
        }
    }
    return std::nullopt;
}
template<int dim>
const FieldGroupSpace<dim>& SpaceSnapshot<dim>::field_space(const PhaseReference phase, const FieldGroupId group) const
{
    const auto& field = data_->field_spaces.at(group.value());
    const auto actual = field.phase();
    if (actual.graph.run != phase.graph.run || actual.graph.graph != phase.graph.graph || actual.phase != phase.phase) {
        throw std::invalid_argument("field group does not belong to the requested phase");
    }
    return field;
}
template<int dim> const StateLayout& SpaceSnapshot<dim>::layout() const noexcept { return data_->layout; }

template<int dim>
SpaceRegistry<dim>::SpaceRegistry(std::shared_ptr<const MeshSnapshot<dim>> mesh) :
    mesh_(std::move(mesh)),
    id_(detail::reserve_space_registry_id(detail::MeshSnapshotAccess<dim>::run_control(*mesh_), mesh_->communicator(),
                                          mesh_->id()))
{
}

template<int dim>
void SpaceRegistry<dim>::validate_field_schema(
    const PhaseGraph& graph, const SpaceSpecification& specification, detail::RepresentedPhaseMap& represented,
    SpaceBuildErrors& errors, const std::shared_ptr<const detail::RunConfigurationControl>& run_control,
    const MPI_Comm communicator) const
{
    if (graph.provenance().run != mesh_->provenance().run) {
        detail::add_space_error(errors, SpaceBuildErrorCode::graph_provenance_mismatch,
                                "the phase graph and mesh snapshot belong to different runs");
    }
    const auto schemas =
        detail::all_gather_space_payloads(run_control, communicator, detail::canonical_space_schema(specification));
    if (!std::ranges::all_of(schemas, [&](const auto& schema) { return schema == schemas.front(); })) {
        detail::add_space_error(errors, SpaceBuildErrorCode::replicated_schema_mismatch,
                                "the replicated phase-field schema differs between ranks");
    }
    std::map<std::pair<std::uint32_t, std::string>, std::size_t> occurrences;
    for (const auto& field : specification.phase_fields) {
        if (!graph.owns(field.phase)) {
            const auto owner = graph.provenance();
            const auto code = field.phase.graph.run == owner.run && field.phase.graph.graph == owner.graph
                                  ? SpaceBuildErrorCode::unknown_phase
                                  : SpaceBuildErrorCode::phase_reference_mismatch;
            detail::add_space_error(
                errors, code, "field group '" + field.name + "' carries an unresolved phase reference", field.phase);
        }
        else {
            represented.insert_or_assign(field.phase.phase.value(), field.phase);
        }
        if (field.name.empty()) {
            detail::add_space_error(errors, SpaceBuildErrorCode::empty_field_name,
                                    "a phase field group has an empty name");
        }
        ++occurrences[{field.phase.phase.value(), field.name}];
        if (field.components == 0) {
            detail::add_space_error(errors, SpaceBuildErrorCode::zero_components,
                                    "field group '" + field.name + "' has zero components");
        }
        if (field.polynomial_degree == 0) {
            detail::add_space_error(errors, SpaceBuildErrorCode::zero_polynomial_degree,
                                    "field group '" + field.name + "' has polynomial degree zero");
        }
    }
    for (const auto& [key, count] : occurrences) {
        if (count > 1) {
            detail::add_space_error(errors, SpaceBuildErrorCode::duplicate_field_name,
                                    "phase " + std::to_string(key.first) + " declares field group '" + key.second +
                                        "' more than once");
        }
    }
    if (specification.level_set.name.empty()) {
        detail::add_space_error(errors, SpaceBuildErrorCode::empty_level_set_name,
                                "the level-set field group has an empty name");
    }
    if (specification.level_set.components == 0) {
        detail::add_space_error(errors, SpaceBuildErrorCode::zero_level_set_components,
                                "the level-set field group has zero components");
    }
    if (specification.level_set.polynomial_degree == 0) {
        detail::add_space_error(errors, SpaceBuildErrorCode::zero_level_set_polynomial_degree,
                                "the level-set field group has polynomial degree zero");
    }
}

template<int dim>
void SpaceRegistry<dim>::validate_support_record(const PhaseGraph& graph, const PhaseSupportSpecification& support,
                                                 const detail::RepresentedPhaseMap& represented,
                                                 const SupportEnvelope& locally_owned_active,
                                                 const SupportEnvelope& globally_active,
                                                 const SupportEnvelope& globally_inactive,
                                                 SpaceBuildErrors& errors) const
{
    if (!graph.owns(support.phase)) {
        const auto owner = graph.provenance();
        const auto code = support.phase.graph.run == owner.run && support.phase.graph.graph == owner.graph
                              ? SpaceBuildErrorCode::unknown_phase
                              : SpaceBuildErrorCode::phase_reference_mismatch;
        detail::add_space_error(errors, code, "a phase-support record carries an unresolved phase reference",
                                support.phase);
    }
    if (support.mesh != mesh_->id()) {
        detail::add_space_error(errors, SpaceBuildErrorCode::support_mesh_mismatch,
                                "a phase-support record belongs to another mesh snapshot", support.phase);
    }
    if (!represented.contains(support.phase.phase.value())) {
        detail::add_space_error(
            errors, SpaceBuildErrorCode::unused_phase_support,
            "phase " + std::to_string(support.phase.phase.value()) + " has support but no field group", support.phase);
    }
    for (const auto& cell : support.locally_owned_requested_cells) {
        if (locally_owned_active.contains(cell)) {
            continue;
        }
        if (globally_active.contains(cell)) {
            detail::add_space_error(errors, SpaceBuildErrorCode::nonowned_support_cell,
                                    "phase support requests nonowned active cell '" + cell.to_string() + "'",
                                    support.phase, cell);
        }
        else if (globally_inactive.contains(cell)) {
            detail::add_space_error(errors, SpaceBuildErrorCode::inactive_support_cell,
                                    "phase support requests inactive cell '" + cell.to_string() + "'", support.phase,
                                    cell);
        }
        else {
            detail::add_space_error(errors, SpaceBuildErrorCode::unknown_support_cell,
                                    "phase support requests unknown cell '" + cell.to_string() + "'", support.phase,
                                    cell);
        }
    }
}

template<int dim>
detail::SupportsByPhase SpaceRegistry<dim>::validate_support_schema(
    const PhaseGraph& graph, const std::vector<PhaseSupportSpecification>& supports,
    const detail::RepresentedPhaseMap& represented, const SupportEnvelope& locally_owned_active,
    const SupportEnvelope& globally_active, const SupportEnvelope& globally_inactive, SpaceBuildErrors& errors) const
{
    detail::SupportsByPhase support_by_phase;
    for (const auto& support : supports) {
        support_by_phase[support.phase.phase.value()].push_back(&support);
        validate_support_record(graph, support, represented, locally_owned_active, globally_active, globally_inactive,
                                errors);
    }
    for (const auto& [phase, reference] : represented) {
        static_cast<void>(reference);
        const auto count = support_by_phase[phase].size();
        if (count == 0) {
            detail::add_space_error(errors, SpaceBuildErrorCode::missing_phase_support,
                                    "phase " + std::to_string(phase) + " has no support record", reference);
        }
        else if (count > 1) {
            detail::add_space_error(errors, SpaceBuildErrorCode::duplicate_phase_support,
                                    "phase " + std::to_string(phase) + " has multiple support records", reference);
        }
    }
    return support_by_phase;
}

template<int dim>
std::expected<std::shared_ptr<const PhaseSupport>, SpaceBuildError>
SpaceRegistry<dim>::close_phase_support(const PhaseReference& reference, const PhaseSupportSpecification& support,
                                        const SupportEnvelope& locally_owned_active,
                                        const std::optional<dealii::types::global_cell_index> closure_iteration_limit,
                                        const std::shared_ptr<const detail::RunConfigurationControl>& run_control,
                                        const MPI_Comm communicator) const
{
    const auto& requested = support.locally_owned_requested_cells;
    SupportEnvelope final_mask = requested;
    bool converged = false;
    const auto maximum_iterations =
        closure_iteration_limit.value_or(mesh_->triangulation().n_global_active_cells() + 1);
    for (dealii::types::global_cell_index iteration = 0; iteration < maximum_iterations; ++iteration) {
        const auto global_mask = detail::collective_cell_union(run_control, communicator, final_mask);
        const auto requested_additions = detail::hanging_face_additions(mesh_->triangulation(), global_mask);
        const auto global_additions = detail::collective_cell_union(run_control, communicator, requested_additions);
        int local_changed = 0;
        for (const auto& cell : global_additions) {
            if (locally_owned_active.contains(cell) && final_mask.insert(cell).second) {
                local_changed = 1;
            }
        }
        if (detail::collective_space_changed(run_control, communicator, local_changed) == 0) {
            converged = true;
            break;
        }
    }
    if (!converged) {
        return std::unexpected(SpaceBuildError{.code = SpaceBuildErrorCode::closure_nonconvergence,
                                               .message = "phase-support closure did not reach a fixed point",
                                               .phase = reference,
                                               .cell = std::nullopt,
                                               .reporting_rank = std::nullopt});
    }
    SupportEnvelope added;
    for (const auto& cell : final_mask) {
        if (!requested.contains(cell)) {
            added.insert(cell);
        }
    }
    return PhaseSupport::create(reference, mesh_->id(), requested, std::move(added), std::move(final_mask));
}

template<int dim>
std::expected<detail::ClosedPhaseSupports, SpaceBuildErrors> SpaceRegistry<dim>::close_phase_supports(
    const detail::RepresentedPhaseMap& represented, const detail::SupportsByPhase& support_by_phase,
    const SupportEnvelope& locally_owned_active,
    const std::optional<dealii::types::global_cell_index> closure_iteration_limit,
    const std::shared_ptr<const detail::RunConfigurationControl>& run_control, const MPI_Comm communicator) const
{
    detail::ClosedPhaseSupports closed;
    SpaceBuildErrors errors;
    for (const auto& [phase_id, reference] : represented) {
        auto support = close_phase_support(reference, *support_by_phase.at(phase_id).front(), locally_owned_active,
                                           closure_iteration_limit, run_control, communicator);
        if (!support) {
            errors.push_back(std::move(support.error()));
            break;
        }
        closed.emplace(phase_id, std::move(*support));
    }
    if (!errors.empty()) {
        return std::unexpected(detail::collective_space_errors(run_control, communicator, errors));
    }
    return closed;
}

template<int dim>
SpaceDraft<dim> SpaceRegistry<dim>::assemble_draft(SpaceSpecification specification, const SpaceProvenance provenance,
                                                   const detail::ClosedPhaseSupports& closed_supports) const
{
    std::ranges::sort(specification.phase_fields, [](const auto& left, const auto& right) {
        return std::pair(left.phase.phase.value(), left.name) < std::pair(right.phase.phase.value(), right.name);
    });
    std::vector<FieldGroupSpace<dim>> field_spaces;
    field_spaces.reserve(specification.phase_fields.size());
    for (auto& field : specification.phase_fields) {
        const auto id = FieldGroupId::from_index(static_cast<std::uint32_t>(field_spaces.size()));
        field_spaces.push_back(FieldGroupSpace<dim>(
            detail::build_field_space(mesh_, provenance, id, field.phase, std::move(field.name), field.components,
                                      field.polynomial_degree, closed_supports.at(field.phase.phase.value()), false),
            field.phase));
    }
    const auto level_set_id = FieldGroupId::from_index(static_cast<std::uint32_t>(field_spaces.size()));
    LevelSetFieldSpace<dim> level_set_space(detail::build_field_space(
        mesh_, provenance, level_set_id, std::nullopt, std::move(specification.level_set.name),
        specification.level_set.components, specification.level_set.polynomial_degree, {}, true));
    auto data = std::make_shared<detail::SpaceDraftData<dim>>(mesh_, provenance, std::move(field_spaces),
                                                              std::move(level_set_space));
    return SpaceDraft<dim>(std::move(data));
}

template<int dim>
SpaceDraftResult<dim> SpaceRegistry<dim>::begin_draft(const PhaseGraph& graph, SpaceSpecification specification,
                                                      std::vector<PhaseSupportSpecification> supports) const
{
    const MPI_Comm communicator = mesh_->communicator();
    const auto run_control = detail::MeshSnapshotAccess<dim>::run_control(*mesh_);
    return begin_draft_with_reserved(graph, std::move(specification), std::move(supports),
                                     detail::reserve_space_epoch(run_control, communicator, mesh_->id()), std::nullopt,
                                     detail::no_space_post_agreement_hook);
}

template<int dim>
SpaceDraftResult<dim> SpaceRegistry<dim>::begin_draft_with_reserved(
    const PhaseGraph& graph, SpaceSpecification specification, std::vector<PhaseSupportSpecification> supports,
    std::expected<SpaceEpoch, SpaceBuildError> reserved,
    const std::optional<dealii::types::global_cell_index> closure_iteration_limit,
    const detail::SpacePostAgreementHook post_agreement_hook) const
{
    const MPI_Comm communicator = mesh_->communicator();
    const auto run_control = detail::MeshSnapshotAccess<dim>::run_control(*mesh_);
    if (!reserved) {
        return std::unexpected(SpaceBuildErrors{reserved.error()});
    }
    return detail::invoke_space_fatal_boundary(run_control, [&]() -> SpaceDraftResult<dim> {
        const SpaceProvenance provenance{.run = mesh_->provenance().run,
                                         .graph = graph.provenance().graph,
                                         .mesh = mesh_->id(),
                                         .registry = id_,
                                         .epoch = *reserved};
        SpaceBuildErrors errors;
        detail::RepresentedPhaseMap represented;
        validate_field_schema(graph, specification, represented, errors, run_control, communicator);

        SupportEnvelope locally_owned_active;
        for (const auto& cell : mesh_->triangulation().active_cell_iterators()) {
            if (cell->is_locally_owned()) {
                locally_owned_active.insert(cell->id());
            }
        }
        const SupportEnvelope globally_active =
            detail::collective_cell_union(run_control, communicator, locally_owned_active);
        SupportEnvelope locally_inactive;
        for (const auto& cell : mesh_->triangulation().cell_iterators()) {
            if (!cell->is_active()) {
                locally_inactive.insert(cell->id());
            }
        }
        const SupportEnvelope globally_inactive =
            detail::collective_cell_union(run_control, communicator, locally_inactive);
        const auto support_by_phase = validate_support_schema(graph, supports, represented, locally_owned_active,
                                                              globally_active, globally_inactive, errors);

        errors = detail::collective_space_errors(run_control, communicator, errors);
        if (!errors.empty()) {
            return std::unexpected(std::move(errors));
        }

        auto closed_supports = close_phase_supports(represented, support_by_phase, locally_owned_active,
                                                    closure_iteration_limit, run_control, communicator);
        if (!closed_supports) {
            return std::unexpected(std::move(closed_supports.error()));
        }
        post_agreement_hook();
        return assemble_draft(std::move(specification), provenance, *closed_supports);
    });
}

template<int dim>
SpaceSnapshotResult<dim>
SpaceRegistry<dim>::finalize(SpaceDraft<dim>& draft,
                             std::vector<RegionalEntrySpecification> regional_specifications) const
{
    return finalize_with_hook(draft, std::move(regional_specifications), detail::no_space_post_agreement_hook);
}

template<int dim>
SpaceSnapshotResult<dim>
SpaceRegistry<dim>::finalize_with_hook(SpaceDraft<dim>& draft,
                                       std::vector<RegionalEntrySpecification> regional_specifications,
                                       const detail::SpacePostAgreementHook post_agreement_hook) const
{
    const auto draft_data = draft.data_;
    const MPI_Comm communicator = mesh_->communicator();
    const auto run_control = detail::MeshSnapshotAccess<dim>::run_control(*mesh_);
    auto result = detail::invoke_space_fatal_boundary(run_control, [&]() -> SpaceSnapshotResult<dim> {
        auto errors = detail::validate_regional_schema(run_control, communicator, regional_specifications);
        if (draft_data == nullptr) {
            detail::add_space_error(errors, SpaceBuildErrorCode::inactive_draft,
                                    "a moved-from or already finalized draft is inactive");
        }
        else if (draft_data->provenance.registry != id_ || draft_data->provenance.mesh != mesh_->id()) {
            detail::add_space_error(errors, SpaceBuildErrorCode::foreign_registry_draft,
                                    "a registry cannot finalize a draft created by another registry");
        }
        errors = detail::collective_space_errors(run_control, communicator, errors);
        if (!errors.empty()) {
            return std::unexpected(std::move(errors));
        }
        post_agreement_hook();

        std::sort(regional_specifications.begin(), regional_specifications.end(),
                  [](const auto& left, const auto& right) { return left.name < right.name; });

        std::vector<StateFieldBlock> blocks;
        blocks.reserve(draft_data->field_spaces.size() + 1);
        for (const auto& field : draft_data->field_spaces) {
            blocks.push_back({field.id(), field.phase(), std::string(field.name()),
                              field.dof_handler().locally_owned_dofs(), false});
        }
        blocks.push_back({draft_data->level_set_space.id(), std::nullopt,
                          std::string(draft_data->level_set_space.name()),
                          draft_data->level_set_space.dof_handler().locally_owned_dofs(), true});

        std::vector<RegionalEntry> regional_entries;
        regional_entries.reserve(regional_specifications.size());
        for (auto& specification : regional_specifications) {
            dealii::IndexSet locally_owned(1);
            if (dealii::Utilities::MPI::this_mpi_process(communicator) == 0) {
                locally_owned.add_index(0);
            }
            locally_owned.compress();
            regional_entries.push_back(
                {.id = RegionalEntryId::from_index(static_cast<std::uint32_t>(regional_entries.size())),
                 .name = std::move(specification.name),
                 .locally_owned_entries = std::move(locally_owned)});
        }

        StateLayout layout(draft_data->provenance, detail::MeshSnapshotAccess<dim>::run_control(*draft_data->mesh),
                           draft_data->mesh, std::move(blocks), std::move(regional_entries),
                           draft_data->level_set_space.id(), communicator);
        auto data = std::make_shared<detail::SpaceSnapshotData<dim>>(draft_data->mesh, draft_data->provenance,
                                                                     draft_data->field_spaces,
                                                                     draft_data->level_set_space, std::move(layout));
        return SpaceSnapshot<dim>(std::move(data));
    });
    if (result) {
        draft.data_.reset();
    }
    return result;
}

} // namespace rift
