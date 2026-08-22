#pragma once

/**
 * \file
 * \brief Phase-local deal.II spaces and centrally versioned discrete state.
 */

#include <algorithm>
#include <atomic>
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
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <rift/phase_graph.hpp>
#include <rift/strong_id.hpp>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
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

/** \brief Distinguish immutable state snapshots from accepted state epochs. */
struct StateSnapshotIdTag {};

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

/** \brief Store immutable vectors and version stamps shared by state snapshots. */
struct StateSnapshotData;

/** \brief Store transaction-private vectors until a trial is sealed or abandoned. */
struct MutableStateData;

} // namespace detail

/** \brief Stable identity of one phase-local or full-background field group. */
using FieldGroupId = StrongId<detail::FieldGroupIdTag>;

/** \brief Stable identity of one finalized regional scalar entry. */
using RegionalEntryId = StrongId<detail::RegionalEntryIdTag>;

/** \brief Identity of one immutable finite-element layout generation. */
using SpaceEpoch = StrongId<detail::SpaceEpochTag, std::uint64_t>;

/** \brief Unique identity of one accepted or private immutable state snapshot. */
using StateSnapshotId = StrongId<detail::StateSnapshotIdTag, std::uint64_t>;

/** \brief Identity of one revision published as live accepted state. */
using StateEpoch = StrongId<detail::StateEpochTag, std::uint64_t>;

/** \brief Identity of the level-set field values used by geometry. */
using LevelSetFieldSetSnapshotId = StrongId<detail::LevelSetFieldSetSnapshotIdTag, std::uint64_t>;

/** \brief Native distributed vector owned by Rift state snapshots. */
using DistributedStateVector = dealii::LinearAlgebra::distributed::Vector<double>;

/** \brief Stable set of active background cells carrying real phase unknowns. */
using SupportEnvelope = std::set<dealii::CellId>;

/**
 * \brief Describe one phase-local continuous-Galerkin field group to build.
 *
 * \code{.cpp}
 * rift::PhaseFieldGroupSpecification flow{
 *     gas, "flow", 5, 2, support_cells};
 * \endcode
 *
 * `support_envelope` uses stable deal.II cell identities. Cells outside it
 * receive non-dominating `FE_Nothing`.
 * \ingroup discrete_state
 */
struct PhaseFieldGroupSpecification {
    /** \brief Phase that owns this field group. */
    PhaseId phase;
    /** \brief Name unique among field groups of the same phase. */
    std::string name;
    /** \brief Number of finite-element components. */
    unsigned int components;
    /** \brief Uniform polynomial degree used on every supported cell. */
    unsigned int polynomial_degree;
    /** \brief Cells on which this group owns real degrees of freedom. */
    SupportEnvelope support_envelope;
};

/**
 * \brief Describe the single full-background level-set field group.
 *
 * \code{.cpp}
 * rift::LevelSetFieldGroupSpecification level_sets{
 *     "level_sets", 2, 1};
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
 * rift::SpaceSpecification specification{
 *     .phase_fields = {{gas, "flow", 5, 2, support_cells}},
 *     .level_set = {"level_sets", 1, 1},
 * };
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
 * std::vector<rift::RegionalEntrySpecification> regional{
 *     {"closed_region_pressure"}, {"pressure_compatibility"}};
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
enum class SpaceBuildErrorCode : std::uint8_t{
    /** A phase field names a `PhaseId` absent from the supplied graph. */
    unknown_phase,
    /** A phase-local field group has no diagnostic/configuration name. */
    empty_field_name,
    /** Two groups owned by the same phase use the same name. */
    duplicate_field_name,
    /** A phase-local field group requests no finite-element components. */
    zero_components,
    /** A phase-local field group requests polynomial degree zero. */
    zero_polynomial_degree,
    /** A support envelope names a cell that is not active on the mesh. */
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
 * for (const rift::SpaceBuildError &error : result.error())
 *     std::cerr << error.message << '\n';
 * \endcode
 * \ingroup discrete_state
 */
struct SpaceBuildError {
    /** \brief Machine-readable error classification. */
    SpaceBuildErrorCode code;
    /** \brief Human-readable explanation naming the invalid entity. */
    std::string message;
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
    std::optional<PhaseId> phase;
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
 * rift::StateStore store(space_snapshot.layout());
 * const auto accepted = store.snapshot(rift::StateSlot::accepted);
 * \endcode
 *
 * The layout is immutable and carries the communicator and `SpaceEpoch` used
 * to initialize every central state vector.
 * \ingroup discrete_state
 */
class StateLayout {
public:
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

private:
    template<int dim> friend class SpaceRegistry;

    /** \brief Construct a validated final layout from a completed space draft. */
    StateLayout(SpaceEpoch space_epoch, std::vector<StateFieldBlock> field_blocks,
                std::vector<RegionalEntry> regional_entries, FieldGroupId level_set_group, MPI_Comm communicator) :
        space_epoch_(space_epoch),
        field_blocks_(std::move(field_blocks)),
        regional_entries_(std::move(regional_entries)),
        level_set_group_(level_set_group),
        communicator_(communicator)
    {
    }

    /** \brief Epoch shared by every partition in the layout. */
    SpaceEpoch space_epoch_;
    /** \brief Field vector partitioners indexed by field-group identity. */
    std::vector<StateFieldBlock> field_blocks_;
    /** \brief Rank-zero-owned scalar partitioners indexed by regional identity. */
    std::vector<RegionalEntry> regional_entries_;
    /** \brief Identity of the field block whose values define geometry. */
    FieldGroupId level_set_group_;
    /** \brief Non-owning communicator handle used to initialize vectors. */
    MPI_Comm communicator_;
};

/**
 * \brief Inspect one immutable phase-local field-group space.
 *
 * \par Typical use
 * \code{.cpp}
 * const auto flow_id = snapshot.find_field(gas, "flow").value();
 * const auto &flow = snapshot.field_space(gas, flow_id);
 * std::cout << flow.dof_handler().n_dofs();
 * \endcode
 *
 * The returned deal.II objects borrow the mesh retained by their snapshot and
 * remain valid only while that space generation remains alive.
 * \ingroup discrete_state
 */
template<int dim> class FieldGroupSpace {
public:
    /** \brief Return the owning phase identity. */
    [[nodiscard]] PhaseId phase() const noexcept;
    /** \brief Return the stable field-group identity. */
    [[nodiscard]] FieldGroupId id() const noexcept;
    /** \brief Return the diagnostic field-group name. */
    [[nodiscard]] std::string_view name() const noexcept;
    /** \brief Return the number of finite-element components. */
    [[nodiscard]] unsigned int components() const noexcept;
    /** \brief Return the uniform polynomial degree on supported cells. */
    [[nodiscard]] unsigned int polynomial_degree() const noexcept;
    /** \brief Return the immutable solve-time support envelope. */
    [[nodiscard]] const SupportEnvelope& support_envelope() const noexcept;
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
    explicit FieldGroupSpace(std::shared_ptr<const detail::FieldGroupSpaceData<dim>> data) : data_(std::move(data)) {}

    /** \brief Stable storage retaining the mesh, FE collection, and deal.II objects. */
    std::shared_ptr<const detail::FieldGroupSpaceData<dim>> data_;
};

/**
 * \brief Inspect the full-background level-set finite-element space.
 *
 * \code{.cpp}
 * const auto &level_sets = snapshot.level_set_space();
 * for (const auto &cell : level_sets.dof_handler().active_cell_iterators())
 *     assert(cell->get_fe().dofs_per_cell > 0);
 * \endcode
 *
 * This space never contains `FE_Nothing` and therefore remains defined on the
 * complete background mesh.
 * \ingroup discrete_state
 */
template<int dim> class LevelSetFieldSpace {
public:
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
 * auto draft = registry.begin_draft(graph, specification);
 * if (draft)
 *     geometry.reconstruct(draft->level_set_space().dof_handler());
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

    /** \brief Return the unique provisional epoch reserved for this draft. */
    [[nodiscard]] SpaceEpoch epoch() const noexcept;
    /** \brief Iterate over provisional phase-local spaces. */
    [[nodiscard]] std::span<const FieldGroupSpace<dim>> field_spaces() const noexcept;
    /** \brief Borrow the provisional full-background level-set space. */
    [[nodiscard]] const LevelSetFieldSpace<dim>& level_set_space() const noexcept;

private:
    friend class SpaceRegistry<dim>;

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
 * auto draft = registry.begin_draft(graph, specification);
 * auto snapshot = registry.finalize(std::move(*draft), regional_entries);
 * rift::StateStore state(snapshot->layout());
 * \endcode
 *
 * References into the snapshot become stale when a later rebuild is selected
 * as the live `SpaceEpoch`.
 * \ingroup discrete_state
 */
template<int dim> class SpaceSnapshot {
public:
    /** \brief Return this immutable finite-element generation's identity. */
    [[nodiscard]] SpaceEpoch epoch() const noexcept;
    /** \brief Iterate over phase-local spaces in `FieldGroupId` order. */
    [[nodiscard]] std::span<const FieldGroupSpace<dim>> field_spaces() const noexcept;
    /** \brief Borrow the one full-background level-set space. */
    [[nodiscard]] const LevelSetFieldSpace<dim>& level_set_space() const noexcept;
    /** \brief Find a phase-local group by its configuration name. */
    [[nodiscard]] std::optional<FieldGroupId> find_field(PhaseId phase, std::string_view name) const noexcept;
    /** \brief Resolve and phase-check a field-group identity. */
    [[nodiscard]] const FieldGroupSpace<dim>& field_space(PhaseId phase, FieldGroupId group) const;
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
 * auto mesh = std::make_shared<dealii::Triangulation<2>>();
 * dealii::GridGenerator::hyper_cube(*mesh);
 * rift::SpaceRegistry<2> registry(mesh, MPI_COMM_SELF);
 * auto draft = registry.begin_draft(graph, specification);
 * auto snapshot = registry.finalize(std::move(*draft), {});
 * \endcode
 *
 * The registry retains shared ownership of the triangulation so every
 * `DoFHandler` remains attached to a live mesh. Rejected drafts still consume
 * their provisional epoch.
 * \ingroup discrete_state
 */
template<int dim> class SpaceRegistry {
public:
    /**
     * \brief Select the background mesh and communicator used by all groups.
     *
     * \param triangulation shared background mesh that outlives every snapshot.
     * \param communicator communicator used by constraints and vector partitions.
     */
    SpaceRegistry(std::shared_ptr<dealii::Triangulation<dim>> triangulation, MPI_Comm communicator) :
        triangulation_(std::move(triangulation)), communicator_(communicator)
    {
    }

    /** \brief Validate a schema and construct all provisional field spaces. */
    [[nodiscard]] SpaceDraftResult<dim> begin_draft(const PhaseGraph& graph, SpaceSpecification specification) const;

    /** \brief Add regional scalar blocks and publish a complete immutable layout. */
    [[nodiscard]] SpaceSnapshotResult<dim> finalize(SpaceDraft<dim>&& draft,
                                                    std::vector<RegionalEntrySpecification> regional_entries) const;

private:
    /** \brief Shared mesh retained by all constructed space data. */
    std::shared_ptr<dealii::Triangulation<dim>> triangulation_;
    /** \brief Non-owning communicator handle for this run. */
    MPI_Comm communicator_;
};

/**
 * \brief Stamp one immutable state with its space, snapshot, and publication identity.
 *
 * \code{.cpp}
 * const rift::StateSnapshotStamp stamp = snapshot.stamp();
 * if (stamp.published_epoch)
 *     std::cout << "accepted";
 * \endcode
 * \ingroup discrete_state
 */
struct StateSnapshotStamp {
    /** \brief Finite-element generation required to interpret vector indices. */
    SpaceEpoch space;
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
 * const rift::StateSnapshot accepted =
 *     store.snapshot(rift::StateSlot::accepted);
 * const auto &flow = accepted.field(flow_group);
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
    /** \brief Borrow an immutable phase or level-set field vector. */
    [[nodiscard]] const DistributedStateVector& field(FieldGroupId group) const;
    /** \brief Borrow an immutable regional scalar vector. */
    [[nodiscard]] const DistributedStateVector& regional(RegionalEntryId entry) const;

private:
    friend class StateStore;
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
 * auto trial = store.begin_trial(base.stamp().snapshot);
 * trial.field(flow_group) += update;
 * const rift::StateSnapshot candidate = trial.seal();
 * \endcode
 *
 * A transaction is move-only. `seal()` creates a new private snapshot;
 * `abandon()` and destruction discard unsealed mutable storage.
 * \ingroup discrete_state
 */
class MutableStateTransaction {
public:
    /** \brief Transfer the sole mutable authority and leave `other` inactive. */
    MutableStateTransaction(MutableStateTransaction&&) noexcept;
    /** \brief Replace this trial with `other` and leave `other` inactive. */
    MutableStateTransaction& operator=(MutableStateTransaction&&) noexcept;
    /** \brief Prevent two transactions from sharing mutable state. */
    MutableStateTransaction(const MutableStateTransaction&) = delete;
    /** \brief Prevent assignment from creating a mutable alias. */
    MutableStateTransaction& operator=(const MutableStateTransaction&) = delete;
    /** \brief Discard unsealed private storage without publishing it. */
    ~MutableStateTransaction();

    /** \brief Borrow a mutable transaction-private field vector. */
    [[nodiscard]] DistributedStateVector& field(FieldGroupId group);
    /** \brief Borrow a mutable transaction-private regional scalar vector. */
    [[nodiscard]] DistributedStateVector& regional(RegionalEntryId entry);
    /** \brief Freeze the vectors as a uniquely identified private snapshot. */
    [[nodiscard]] StateSnapshot seal();
    /** \brief Drop mutable storage without changing any retained snapshot. */
    void abandon() noexcept;
    /** \brief Report whether mutable access and sealing remain valid. */
    [[nodiscard]] bool active() const noexcept;

private:
    friend class StateStore;

    /** \brief Start with a private copy of one retained base snapshot. */
    MutableStateTransaction(StateStore& owner, std::unique_ptr<detail::MutableStateData> data);

    /** \brief Store that allocates identities and retains sealed candidates. */
    StateStore* owner_;
    /** \brief Transaction-private mutable vectors, absent after completion. */
    std::unique_ptr<detail::MutableStateData> data_;
};

/**
 * \brief Own accepted, previous, and private state snapshots for one layout.
 *
 * \par Typical use
 * \code{.cpp}
 * rift::StateStore store(space.layout());
 * const auto accepted = store.snapshot(rift::StateSlot::accepted);
 * auto trial = store.begin_trial(accepted.stamp().snapshot);
 * trial.field(flow_group) = 1.0;
 * const auto candidate = trial.seal();
 * const auto published = store.publish(candidate.stamp().snapshot);
 * \endcode
 *
 * Publication advances `StateEpoch` without changing `SpaceEpoch`. Private
 * candidates can be retained as nonlinear base points or explicitly discarded.
 * \ingroup discrete_state
 */
class StateStore {
public:
    /** \brief Allocate a zero-initialized accepted state for a finalized layout. */
    explicit StateStore(StateLayout layout);
    /** \brief Prevent two authorities from owning the same publication history. */
    StateStore(const StateStore&) = delete;
    /** \brief Prevent assignment from duplicating publication authority. */
    StateStore& operator=(const StateStore&) = delete;
    /** \brief Keep transaction owner pointers stable by prohibiting store relocation. */
    StateStore(StateStore&&) = delete;
    /** \brief Keep transaction owner pointers stable by prohibiting store relocation. */
    StateStore& operator=(StateStore&&) = delete;
    /** \brief Release all retained snapshots after transactions have finished. */
    ~StateStore();

    /** \brief Return the immutable accepted or previous publication slot. */
    [[nodiscard]] StateSnapshot snapshot(StateSlot slot) const;
    /** \brief Return any retained snapshot by unique identity. */
    [[nodiscard]] StateSnapshot snapshot(StateSnapshotId id) const;
    /** \brief Copy a retained base into an isolated mutable transaction. */
    [[nodiscard]] MutableStateTransaction begin_trial(StateSnapshotId base);
    /** \brief Publish a retained private candidate as the accepted state. */
    [[nodiscard]] StateSnapshot publish(StateSnapshotId candidate);
    /** \brief Stop retaining a private snapshot. */
    void discard(StateSnapshotId candidate);
    /** \brief Return the immutable layout generation owned by this store. */
    [[nodiscard]] SpaceEpoch space_epoch() const noexcept;

private:
    friend class MutableStateTransaction;

    /** \brief Seal and retain the mutable storage of one active transaction. */
    [[nodiscard]] StateSnapshot seal(MutableStateTransaction& transaction);

    /** \brief Hide snapshot maps, counters, and the retained finalized layout. */
    struct Impl;
    /** \brief Unique state authority implementation. */
    std::unique_ptr<Impl> impl_;
};

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
 * auto data = rift::detail::build_field_space<2>(
 *     mesh, MPI_COMM_SELF, group, phase, "flow", 4, 1,
 *     support_envelope, epoch, false);
 * const auto locally_owned = data->dof_handler->locally_owned_dofs();
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
template<int dim> struct FieldGroupSpaceData {
    /** \brief Shared mesh whose lifetime encloses the attached DoFHandler. */
    std::shared_ptr<dealii::Triangulation<dim>> triangulation;
    /** \brief Stable index used by the finalized state layout. */
    FieldGroupId id;
    /** \brief Owning phase, absent only for the full-background level-set group. */
    std::optional<PhaseId> phase;
    /** \brief Diagnostic field-group name retained from validated configuration. */
    std::string name;
    /** \brief Number of scalar components represented by each real element. */
    unsigned int components;
    /** \brief Uniform degree used by every real element in this group. */
    unsigned int polynomial_degree;
    /** \brief Active-cell identities selecting the real phase-local element. */
    SupportEnvelope support_envelope;
    /** \brief Never-reused generation shared by this group's numbering and constraints. */
    SpaceEpoch epoch;
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
 * auto draft_result = registry.begin_draft(graph, specification);
 * if (draft_result) {
 *     const auto epoch = draft_result->epoch();
 *     const auto &level_sets = draft_result->level_set_space();
 *     reconstruct_geometry(epoch, level_sets.dof_handler());
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
template<int dim> struct SpaceDraftData {
    /** \brief Mesh retained for the complete draft lifetime. */
    std::shared_ptr<dealii::Triangulation<dim>> triangulation;
    /** \brief Provisional generation reserved before validation begins. */
    SpaceEpoch epoch;
    /** \brief Communicator later copied into the completed state layout. */
    MPI_Comm communicator;
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
 * auto draft = registry.begin_draft(graph, specification);
 * auto space = registry.finalize(std::move(*draft), {{"pressure"}});
 * rift::StateStore state(space->layout());
 * const auto accepted = state.snapshot(rift::StateSlot::accepted);
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
template<int dim> struct SpaceSnapshotData {
    /** \brief Mesh retained while any public space snapshot survives. */
    std::shared_ptr<dealii::Triangulation<dim>> triangulation;
    /** \brief Immutable generation shared by spaces and vector partitions. */
    SpaceEpoch epoch;
    /** \brief Finalized phase-local groups in stable identity order. */
    std::vector<FieldGroupSpace<dim>> field_spaces;
    /** \brief Finalized full-background geometry-field group. */
    LevelSetFieldSpace<dim> level_set_space;
    /** \brief Complete field and regional vector partitioning. */
    StateLayout layout;
};

/**
 * \brief Reserve the next process-local finite-element generation.
 *
 * Maintainers call this before validating a draft so a rejected provisional
 * identity is never recycled and cannot alias stale cached data.
 *
 * \code{.cpp}
 * const rift::SpaceEpoch provisional =
 *     rift::SpaceEpoch::from_index(rift::detail::reserve_space_epoch());
 * \endcode
 *
 * \return monotonically increasing process-local integer representation.
 */
inline std::uint64_t reserve_space_epoch()
{
    static std::atomic<std::uint64_t> next{0};
    return next.fetch_add(1, std::memory_order_relaxed);
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
 */
inline void add_space_error(SpaceBuildErrors& errors, const SpaceBuildErrorCode code, std::string message)
{
    errors.push_back({.code=code, .message=std::move(message)});
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
 *     mesh, MPI_COMM_SELF, rift::FieldGroupId::from_index(0), gas,
 *     "flow", 4, 1, support_envelope, epoch, false);
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
 * \param triangulation shared mesh to retain with the DoFHandler.
 * \param communicator communicator associated with this construction.
 * \param id stable field-group identity assigned by the registry.
 * \param phase owning phase, or no value for the level-set group.
 * \param name validated diagnostic/configuration name.
 * \param components positive component count.
 * \param polynomial_degree positive uniform polynomial degree.
 * \param support_envelope active cells carrying real phase-local elements.
 * \param epoch provisional generation reserved for the surrounding draft.
 * \param full_background whether every cell must use the real element.
 * \return shared immutable implementation storage for a public space wrapper.
 */
template<int dim>
std::shared_ptr<const FieldGroupSpaceData<dim>>
build_field_space(const std::shared_ptr<dealii::Triangulation<dim>>& triangulation, const MPI_Comm communicator,
                  const FieldGroupId id, const std::optional<PhaseId> phase, std::string name,
                  const unsigned int components, const unsigned int polynomial_degree, SupportEnvelope support_envelope,
                  const SpaceEpoch epoch, const bool full_background)
{
    auto data = std::shared_ptr<FieldGroupSpaceData<dim>>(
        new FieldGroupSpaceData<dim>{.triangulation = triangulation,
                                     .id = id,
                                     .phase = phase,
                                     .name = std::move(name),
                                     .components = components,
                                     .polynomial_degree = polynomial_degree,
                                     .support_envelope = std::move(support_envelope),
                                     .epoch = epoch,
                                     .finite_elements = {},
                                     .dof_handler = {},
                                     .constraints = {}});
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

    data->dof_handler = std::make_unique<dealii::DoFHandler<dim>>(*triangulation);
    if (!full_background) {
        for (const auto& cell : data->dof_handler->active_cell_iterators()) {
            cell->set_active_fe_index(data->support_envelope.contains(cell->id()) ? 0 : 1);
}
}

    if (full_background) {
        data->dof_handler->distribute_dofs(data->finite_elements[0]);
    } else {
        data->dof_handler->distribute_dofs(data->finite_elements);
}
    const auto locally_relevant = dealii::DoFTools::extract_locally_relevant_dofs(*data->dof_handler);
    data->constraints.reinit(data->dof_handler->locally_owned_dofs(), locally_relevant);
    dealii::DoFTools::make_hanging_node_constraints(*data->dof_handler, data->constraints);
    data->constraints.close();
    static_cast<void>(communicator);
    return data;
}

} // namespace detail

template<int dim> PhaseId FieldGroupSpace<dim>::phase() const noexcept { return data_->phase.value(); }
template<int dim> FieldGroupId FieldGroupSpace<dim>::id() const noexcept { return data_->id; }
template<int dim> std::string_view FieldGroupSpace<dim>::name() const noexcept { return data_->name; }
template<int dim> unsigned int FieldGroupSpace<dim>::components() const noexcept { return data_->components; }
template<int dim> unsigned int FieldGroupSpace<dim>::polynomial_degree() const noexcept
{
    return data_->polynomial_degree;
}
template<int dim> const SupportEnvelope& FieldGroupSpace<dim>::support_envelope() const noexcept
{
    return data_->support_envelope;
}
template<int dim> SpaceEpoch FieldGroupSpace<dim>::epoch() const noexcept { return data_->epoch; }
template<int dim> const dealii::DoFHandler<dim>& FieldGroupSpace<dim>::dof_handler() const noexcept
{
    return *data_->dof_handler;
}
template<int dim> const dealii::AffineConstraints<double>& FieldGroupSpace<dim>::constraints() const noexcept
{
    return data_->constraints;
}

template<int dim> FieldGroupId LevelSetFieldSpace<dim>::id() const noexcept { return data_->id; }
template<int dim> std::string_view LevelSetFieldSpace<dim>::name() const noexcept { return data_->name; }
template<int dim> unsigned int LevelSetFieldSpace<dim>::components() const noexcept { return data_->components; }
template<int dim> unsigned int LevelSetFieldSpace<dim>::polynomial_degree() const noexcept
{
    return data_->polynomial_degree;
}
template<int dim> SpaceEpoch LevelSetFieldSpace<dim>::epoch() const noexcept { return data_->epoch; }
template<int dim> const dealii::DoFHandler<dim>& LevelSetFieldSpace<dim>::dof_handler() const noexcept
{
    return *data_->dof_handler;
}
template<int dim> const dealii::AffineConstraints<double>& LevelSetFieldSpace<dim>::constraints() const noexcept
{
    return data_->constraints;
}

template<int dim> SpaceEpoch SpaceDraft<dim>::epoch() const noexcept { return data_->epoch; }
template<int dim> std::span<const FieldGroupSpace<dim>> SpaceDraft<dim>::field_spaces() const noexcept
{
    return data_->field_spaces;
}
template<int dim> const LevelSetFieldSpace<dim>& SpaceDraft<dim>::level_set_space() const noexcept
{
    return data_->level_set_space;
}

template<int dim> SpaceEpoch SpaceSnapshot<dim>::epoch() const noexcept { return data_->epoch; }
template<int dim> std::span<const FieldGroupSpace<dim>> SpaceSnapshot<dim>::field_spaces() const noexcept
{
    return data_->field_spaces;
}
template<int dim> const LevelSetFieldSpace<dim>& SpaceSnapshot<dim>::level_set_space() const noexcept
{
    return data_->level_set_space;
}
template<int dim>
std::optional<FieldGroupId> SpaceSnapshot<dim>::find_field(const PhaseId phase,
                                                           const std::string_view name) const noexcept
{
    for (const auto& field : data_->field_spaces) {
        if (field.phase() == phase && field.name() == name) {
            return field.id();
}
}
    return std::nullopt;
}
template<int dim>
const FieldGroupSpace<dim>& SpaceSnapshot<dim>::field_space(const PhaseId phase, const FieldGroupId group) const
{
    const auto& field = data_->field_spaces.at(group.value());
    if (field.phase() != phase) {
        throw std::invalid_argument("field group does not belong to the requested phase");
}
    return field;
} // GCOVR_EXCL_LINE -- Clang maps an unreachable exception-cleanup block to this closing brace.
template<int dim> const StateLayout& SpaceSnapshot<dim>::layout() const noexcept { return data_->layout; }

template<int dim>
SpaceDraftResult<dim> SpaceRegistry<dim>::begin_draft(const PhaseGraph& graph, SpaceSpecification specification) const
{
    const auto epoch = SpaceEpoch::from_index(detail::reserve_space_epoch());
    SpaceBuildErrors errors;
    std::set<dealii::CellId> active_cells;
    for (const auto& cell : triangulation_->active_cell_iterators()) {
        active_cells.insert(cell->id());
}

    std::map<std::pair<std::uint32_t, std::string>, std::size_t> occurrences;
    for (const auto& field : specification.phase_fields) {
        if (field.phase.value() >= graph.phases().size()) {
            detail::add_space_error(errors, SpaceBuildErrorCode::unknown_phase,
                                    "field group '" + field.name + "' refers to an unknown phase");
}
        if (field.name.empty()) {
            detail::add_space_error(errors, SpaceBuildErrorCode::empty_field_name,
                                    "a phase field group has an empty name");
}
        ++occurrences[{field.phase.value(), field.name}];
        if (field.components == 0) {
            detail::add_space_error(errors, SpaceBuildErrorCode::zero_components,
                                    "field group '" + field.name + "' has zero components");
}
        if (field.polynomial_degree == 0) {
            detail::add_space_error(errors, SpaceBuildErrorCode::zero_polynomial_degree,
                                    "field group '" + field.name + "' has polynomial degree zero");
}
        for (const auto& cell : field.support_envelope) {
            if (!active_cells.contains(cell)) {
                detail::add_space_error(errors, SpaceBuildErrorCode::unknown_support_cell,
                                        "field group '" + field.name + "' contains support cell '" + cell.to_string() +
                                            "' that is not active on the background mesh");
}
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

    if (!errors.empty()) {
        return std::unexpected(std::move(errors));
}

    std::sort(specification.phase_fields.begin(), specification.phase_fields.end(),
              [](const auto& left, const auto& right) {
                  return std::pair(left.phase.value(), left.name) < std::pair(right.phase.value(), right.name);
              }); // GCOVR_EXCL_LINE -- Clang maps std::sort's unreachable cleanup block to the lambda boundary.

    std::vector<FieldGroupSpace<dim>> field_spaces;
    field_spaces.reserve(specification.phase_fields.size());
    for (auto& field : specification.phase_fields) {
        const auto id = FieldGroupId::from_index(static_cast<std::uint32_t>(field_spaces.size()));
        field_spaces.push_back(FieldGroupSpace<dim>(detail::build_field_space(
            triangulation_, communicator_, id, field.phase, std::move(field.name), field.components,
            field.polynomial_degree, std::move(field.support_envelope), epoch, false)));
    }

    const auto level_set_id = FieldGroupId::from_index(static_cast<std::uint32_t>(field_spaces.size()));
    LevelSetFieldSpace<dim> level_set_space(detail::build_field_space(
        triangulation_, communicator_, level_set_id, std::nullopt, std::move(specification.level_set.name),
        specification.level_set.components, specification.level_set.polynomial_degree, {}, epoch, true));

    auto data = std::make_shared<detail::SpaceDraftData<dim>>(detail::SpaceDraftData<dim>{
        .triangulation = triangulation_,
        .epoch = epoch,
        .communicator = communicator_,
        .field_spaces = std::move(field_spaces),
        .level_set_space = std::move(level_set_space),
    });
    return SpaceDraft<dim>(std::move(data));
}

template<int dim>
SpaceSnapshotResult<dim>
SpaceRegistry<dim>::finalize(SpaceDraft<dim>&& draft,
                             std::vector<RegionalEntrySpecification> regional_specifications) const
{
    SpaceBuildErrors errors;
    std::map<std::string, std::size_t, std::less<>> occurrences;
    for (const auto& entry : regional_specifications) {
        if (entry.name.empty()) {
            detail::add_space_error(errors, SpaceBuildErrorCode::empty_regional_entry_name,
                                    "a regional entry has an empty name");
}
        ++occurrences[entry.name];
    }
    for (const auto& [name, count] : occurrences) {
        if (count > 1) {
            detail::add_space_error(errors, SpaceBuildErrorCode::duplicate_regional_entry_name,
                                    "regional entry '" + name + "' is declared more than once");
}
}
    if (!errors.empty()) {
        return std::unexpected(std::move(errors));
}

    std::sort(regional_specifications.begin(), regional_specifications.end(),
              [](const auto& left, const auto& right) { return left.name < right.name; });

    std::vector<StateFieldBlock> blocks;
    blocks.reserve(draft.data_->field_spaces.size() + 1);
    for (const auto& field : draft.data_->field_spaces) {
        blocks.push_back(
            {field.id(), field.phase(), std::string(field.name()), field.dof_handler().locally_owned_dofs(), false});
}
    blocks.push_back({draft.data_->level_set_space.id(), std::nullopt, std::string(draft.data_->level_set_space.name()),
                      draft.data_->level_set_space.dof_handler().locally_owned_dofs(), true});

    std::vector<RegionalEntry> regional_entries;
    regional_entries.reserve(regional_specifications.size());
    for (auto& specification : regional_specifications) {
        dealii::IndexSet locally_owned(1);
        if (dealii::Utilities::MPI::this_mpi_process(draft.data_->communicator) == 0) {
            locally_owned.add_index(0);
}
        locally_owned.compress();
        regional_entries.push_back({.id=RegionalEntryId::from_index(static_cast<std::uint32_t>(regional_entries.size())),
                                    .name=std::move(specification.name), .locally_owned_entries=std::move(locally_owned)});
    }

    StateLayout layout(draft.data_->epoch, std::move(blocks), std::move(regional_entries),
                       draft.data_->level_set_space.id(), draft.data_->communicator);
    auto data = std::make_shared<detail::SpaceSnapshotData<dim>>(detail::SpaceSnapshotData<dim>{
        .triangulation = draft.data_->triangulation,
        .epoch = draft.data_->epoch,
        .field_spaces = draft.data_->field_spaces,
        .level_set_space = draft.data_->level_set_space,
        .layout = std::move(layout),
    });
    draft.data_.reset();
    return SpaceSnapshot<dim>(std::move(data));
}

} // namespace rift
