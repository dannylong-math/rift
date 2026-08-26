# Phase graph and discrete state scientific-feature refinement

## Recorded starting point

- User scope: apply the scientific-feature workflow retroactively to the
  implementations of `01-phase-graph.md` and `02-discrete-state.md`.
- Starting branch: `discrete-state`.
- Starting revision: `268be4ba78cf61a87205f6636629eeae591ea6ee`.
- Refinement base revision: `268be4ba78cf61a87205f6636629eeae591ea6ee`.
- Upstream `main` revision at intake: `561e3e1`.
- Starting worktree: clean.
- Integration branch: `feature/phase-graph-discrete-state-refinement`.
- Remote operations are out of scope.

## Context and assumptions

The in-scope code establishes runtime phase identities, interface topology,
finite-element space ownership, distributed state storage, and immutable state
transitions. It does not yet implement conservation-law residuals, interface
reconstruction, time integration, or a physical closure.

The context of use is pre-release research software for later sharp-interface
multiphase solver stages. Incorrect identifiers, orientation, support,
ownership, or publication semantics could silently route a later numerical
operator to the wrong phase or stale state; these are treated as high-severity
software-contract failures even though no physical prediction is produced by
these two stages alone.

Approved scope and retained assumptions:

- the accepted architecture pages remain the governing contract;
- the user approved the incremental typed-hardening architecture and its
  necessary public-API and state-revision behavior changes on 2026-08-23;
- source and ABI compatibility are not gates under the repository's current
  pre-release static-library policy;
- runtime/numerical-kernel performance optimization remains out of scope, but
  the user expanded the closing scope on 2026-08-25 to reduce test build time
  without weakening test content or oracles; builds are limited to six jobs by
  default (eight maximum without renewed approval), and consolidated tests use
  meaningful Boost.UT `suite<"suite name">` groupings;
- physical-data validation is not applicable at this foundation stage;
- V&V should cover deterministic construction, topology/orientation
  invariants, 2D/3D finite-element behavior, MPI ownership/reduction
  semantics, and state-transition invariants;
- no dependency installation is authorized.

## Required gates

1. Scientific review of the topology and state assumptions.
2. Architecture conformance review with alternatives and compatibility impact.
3. User approval for any change to accepted architecture, public behavior, or
   coverage exclusions.
4. Test-first implementation of approved corrections.
5. Independent adversarial test audit.
6. Applicable V&V evidence after unit tests pass.
7. Documentation, formatting, and generated documentation build.
8. Independent final quality audit reporting `READY FOR HUMAN REVIEW`.
9. A separate local PR-ready squash branch and evidence-based report.

## Task table

| ID | Dependency | Owner | Branch/worktree | Commit | Status | Acceptance evidence | Records |
|---|---|---|---|---|---|---|---|
| R1 | none | applied-math researcher | read-only integration tree | — | Complete | Structural graph conformance; boundary risks and test properties identified | `verification.md` |
| R2 | none | applied-math researcher | read-only integration tree | — | Complete | Serial conformance; blocking distributed/adaptive gaps identified | `verification.md` |
| A1 | R1, R2 | software architect | integration tree, architecture Markdown only | `310dbb4` | Complete | Option A package approved and recorded on 2026-08-23 | `verification.md` and architecture pages |
| A2 | A1 | implementation engineer, test skeptic | read-only integration tree | `9f2b35c` | Complete | Concrete API approved by user on 2026-08-23 | This plan |
| T0 | concrete API gate | implementation engineer | `task/phase-graph-discrete-state-refinement/test-infrastructure` | `e880447` (merged by `1545e0e`) | Complete | Two-rank CTest and audited LLVM coverage infrastructure pass | `verification.md` |
| T1 | T0 | implementation engineer | `task/phase-graph-discrete-state-refinement/run-graph-provenance` | `d13cbd6` (merged by `e74e4be`) | Complete | Run/graph provenance red-green tests and adversarial MPI audit pass | `verification.md` |
| T2 | T1 | implementation engineer | `task/phase-graph-discrete-state-refinement/deterministic-phase-graph` | `6336584` (merged by `a748f09`) | Complete | Deterministic collective construction, callback, UTF-8, graph-property, and adversarial allocation tests pass | `verification.md` |
| T3 | T1 | implementation engineer | `task/phase-graph-discrete-state-refinement/immutable-mesh-provenance` | `5f0898a` (merged by `c1cf8fa`) | Complete | Immutable mesh ownership/provenance, fatal-cleanup, 2D/3D, and MPI tests pass independent audit | `verification.md` |
| T4 | T3 | implementation engineer | `task/phase-graph-discrete-state-refinement/distributed-adaptive-support` | `eb3af62` (merged by `4e01663`) | Complete | Owner-local distributed adaptive support, hanging closure, constraint V&V, coverage enforcement, and adversarial MPI audit pass | `verification.md` |
| T5 | T4 | implementation engineer | `task/phase-graph-discrete-state-refinement/collective-state-protocol` | `d8f1b8f` (merged by `c559a9f`) | Complete | Collective protocol, provenance, atomicity, lineage, lifetime, model, MPI, and adversarial audits pass | `verification.md` |
| T6 | T5 | implementation engineer | `task/phase-graph-discrete-state-refinement/bounded-state-retention` | `bb0d778` (merged by `e570df7`) | Complete | Bounded pin/transient retention, deterministic model, MPI atomicity, coverage, and independent adversarial audit pass | `verification.md` |
| T7 | T5, T6 | implementation engineer | `task/phase-graph-discrete-state-refinement/exact-regional-state` | `89e52eb` (merged by `35b6fad`) | Complete | Exact regional synchronization, bitwise revision, atomicity, fatal-path, coverage, and independent adversarial audit pass | `verification.md` |
| B1 | T7 | C++ performance engineer | `task/phase-graph-discrete-state-refinement/test-build-optimization` | `6f70b36` (merged by `d0d6287`) | Complete | Six-job clean Debug build work fell by 21 compile/link steps and observed wall time fell 10.29%; 187/187 tests and exact coverage pass; named Boost.UT suites preserve all cases | `performance.md` |
| S1 | baseline through B1 | test skeptic | read-only integration/task trees | — | Complete | T0--T7 and test-layout adversarial audits pass; deterministic manual mutants and tooling gaps recorded | `verification.md` and `performance.md` |
| V1 | T1, S1 | V&V scientist | integration tree, report-only | `b32bf96`, refreshed by `e9c5910` | Complete | C0--C7 reproducible contract-level V&V pass in Debug and Release on final source candidate `afe95dd` | `vv.md` |
| D1 | T1, S1, V1 | implementation engineer | assigned task and integration worktrees | `afe95dd`, `0a2b3b1` | Complete | Public/detail API docs, zero-warning Doxygen, 136-page Sourcey build, formatting, final raw inventories | `verification.md` |
| Q1 | integrated tree | quality-gate auditor | read-only integration tree | `afe95dd` source audit, `cc54cac` docs audit | Complete | Independent final verdict: `READY FOR HUMAN REVIEW` | `verification.md` and `pr-report.md` |
| P1 | Q1 | project manager | `feature/phase-graph-discrete-state-refinement-pr` | this local squash commit | Complete | PR report committed and squashed tree verified identical before handoff | `pr-report.md` |

## Proposed concrete public API gate

Status: **Approved by the user on 2026-08-23.** The declarations below are the
implementation contract for tasks T0–T7. Coverage exclusions remain outside
this approval and require individual evidence and review.

### Shared run context and graph provenance

`RunConfiguration::create(MPI_Comm)` collectively allocates a never-reused
`RunConfigurationId`, duplicates and owns the communicator, and returns a
copyable immutable handle backed by shared control. Meshes, graphs, layouts,
and stores retain that control, so moving or destroying the caller's handle
does not invalidate them. Ordinary destructors contain no state-transition
collectives and all run-owned objects must be released before MPI finalization.

The user approved the discovered communicator boundary on 2026-08-23. Rift
supports live intracommunicators whose complete process group belongs to the
current `MPI_COMM_WORLD`, including world, self, duplicates, splits, Cartesian
communicators, and reordered subgroups. It rejects intercommunicators and
intracommunicators containing dynamic or session processes from an unrelated
MPI world. A run ID is never reused during one MPI execution; no persistence
or uniqueness across separate program executions is claimed. Actual MPI
operation failures are fatal because ranks cannot portably rejoin, while
collectively agreed unsupported-input and finite-exhaustion conditions may be
returned through `std::expected`.

```cpp
class RunConfiguration {
public:
  static std::expected<RunConfiguration, RunConfigurationError>
  create(MPI_Comm communicator);

  [[nodiscard]] RunConfigurationId id() const noexcept;
  [[nodiscard]] MPI_Comm communicator() const noexcept;
};

struct PhaseGraphProvenance {
  RunConfigurationId run;
  PhaseGraphInstanceId graph;
};

struct PhaseReference {
  PhaseGraphProvenance graph;
  PhaseId phase;
};

[[nodiscard]] PhaseGraphResult make_phase_graph(
    const RunConfiguration &run,
    const std::vector<PhaseSpecification> &phases,
    const std::vector<InterfaceSpecification> &interfaces,
    const InterfaceCompatibilityCheck &compatibility = {});
```

Graph construction is collective and first agrees on canonical input and
callback outcomes. Lookups are local afterward. `PhaseGraph` exposes
`provenance()`, `reference(PhaseId)`, and `owns(PhaseReference)`; it remains
copy/move constructible and deletes both assignment operators.

### Immutable mesh and owner-local phase support

The mesh factory consumes its `unique_ptr` on both success and failure. It
accepts serial or distributed triangulations, validates communicator
congruence, and returns an immutable shared snapshot whose private
implementation retains the controlled mutable access deal.II needs during
construction. The factory is collective on the run communicator. Every rank
must pass a triangulation constructed collectively on the same mesh-
communicator context; this is an MPI caller precondition because per-process
congruence checks cannot detect ranks alternating two distinct congruent
contexts. Detectable reordered or unrelated communicators are collective
errors.

```cpp
template<int dim>
[[nodiscard]] std::expected<
    std::shared_ptr<const MeshSnapshot<dim>>, MeshSnapshotError>
make_mesh_snapshot(
    const RunConfiguration &run,
    std::unique_ptr<dealii::Triangulation<dim>> triangulation);

struct PhaseFieldGroupSpecification {
  PhaseReference phase;
  std::string name;
  unsigned int components;
  unsigned int polynomial_degree;
};

struct PhaseSupportSpecification {
  PhaseReference phase;
  MeshSnapshotId mesh;
  std::set<dealii::CellId> locally_owned_requested_cells;
};

template<int dim> class SpaceRegistry {
public:
  explicit SpaceRegistry(
      std::shared_ptr<const MeshSnapshot<dim>> mesh);
};
```

The first implementation uses one support mask per phase, shared by its field
groups. Each support record carries graph and mesh provenance. Nonowned,
artificial, stale, and wrong-mesh requests are collective errors. The registry
computes and records requested, closure-added, and final locally owned cells;
the global envelope is their communicator union.

### T4 implementation gate

Status: **Ready to implement under the approved concrete API.** Independent
architecture and adversarial reviews found no additional scientific or public
API decision requiring user approval.

T4 replaces the current per-field, global-looking support set with exactly one
owner-local `PhaseSupportSpecification` for every phase represented by the
replicated field schema. An empty local request is valid. Missing, duplicate,
or unused phase-support records are collective errors. Declaration order is
irrelevant, and every field group for a phase shares one immutable support-data
object exposing its requested, closure-added, and final locally owned masks.

`SpaceRegistry<dim>` retains `std::shared_ptr<const MeshSnapshot<dim>>` and no
longer borrows a mutable triangulation or independent raw communicator. Draft,
field-space, snapshot, layout, and state-field identities carry checked run,
graph, mesh, phase, and space provenance. A communicator-consistent space
epoch is reserved before logical validation, so rejected drafts consume an ID.
Finalization by a different registry is a collective error. The constructor's
mesh pointer has a documented non-null program precondition: a null pointer
cannot supply a communicator on which ranks could agree, so it is not a
recoverable collective input. Phase-taking and phase-returning accessors use
`PhaseReference`, not a bare numeric `PhaseId`.

For each phase, Rift publishes owner mask values to ghosts, scans non-artificial
hanging interfaces, and returns closure requests to the owning ranks. A global
changed reduction repeats this monotone operation to the least fixed point.
Only owners receive active-FE assignments; deal.II synchronizes ghost indices
during DoF distribution. Artificial cells are never queried for active FE
state. Index zero is the real component-compatible finite element and index one
is a non-dominating, component-compatible `FE_Nothing`. Closed hanging-node
constraints are built from locally owned/relevant DoFs. Level-set fields remain
active on the full mesh.

The collective operation order is epoch reservation, complete local
validation, exact replicated-schema/error agreement, support closure, DoF
distribution, and constraint construction. Expected results are reserved for
agreed logical provenance, schema, cell-ownership, and closure errors. Any MPI
status, deal.II collective exception, or asymmetric post-agreement allocation
failure invokes the retained fatal handler before a peer can enter a later
collective; destructors perform no transition collective.

Logical diagnostics are gathered and returned identically on every rank. They
are ordered deterministically by error-code precedence, phase provenance,
cell-ID bytes, and reporting rank, then exactly deduplicated. The required
error vocabulary covers graph/mesh provenance mismatch, phase-reference and
support-mesh mismatch, replicated-schema mismatch, missing/duplicate/unused
phase support, nonowned or inactive support cells, closure nonconvergence,
space-ID exhaustion, and foreign-registry drafts, in addition to the existing
field and level-set validation errors.

Acceptance requires small 2D/3D serial tests plus genuine two-rank tests for
schema agreement, owner/ghost FE state, nonowned and stale requests, disjoint
phase masks, and a deterministic adaptive hanging interface. The adaptive
oracle requests one fine child and requires exactly all children touching the
coarse face: three final supported cells with one closure addition in 2D, and
five with three additions in 3D. Independent scalar-Q1 global DoF totals are
8 and 22 respectively, rather than the full-background 11 and 31. Constants
and every coordinate-linear function must satisfy the closed hanging
constraints to roundoff. In-scope first-party line, function, and branch
coverage must be 100 percent without a new exclusion or suppression.

### Collective state protocol and explicit retention

Layout provenance supplies the run/store context, so state construction does
not accept a second independent run argument. Recoverable collective logical
failures use `std::expected`; MPI failures and dependency allocation failures
that cannot safely rejoin collective agreement remain fatal.

```cpp
struct StateRetentionPolicy {
  std::size_t max_pinned_private_snapshots;
};

template<class T>
using StateTransitionResult = std::expected<T, StateTransitionError>;

[[nodiscard]] StateTransitionResult<StateStore> make_state_store(
    StateLayout layout, StateRetentionPolicy retention);

class StateStore {
public:
  [[nodiscard]] StateTransitionResult<MutableStateTransaction>
  begin_trial_collective(StateSnapshotId base);
  [[nodiscard]] StateTransitionResult<StateSnapshot>
  publish_collective(StateSnapshotId candidate);
  [[nodiscard]] StateTransitionResult<void>
  pin_collective(StateSnapshotId candidate);
  [[nodiscard]] StateTransitionResult<void>
  unpin_collective(StateSnapshotId candidate);
  [[nodiscard]] StateTransitionResult<void>
  discard_collective(StateSnapshotId candidate);
};

class MutableStateTransaction {
public:
  [[nodiscard]] StateTransitionResult<StateSnapshot> seal_collective();
  [[nodiscard]] StateTransitionResult<void>
  set_regional_value_collective(RegionalEntryId entry, double value);
};

class StateSnapshot {
public:
  [[nodiscard]] double regional_value(RegionalEntryId entry) const;
};
```

Local field-vector access remains a checked noncollective reference operation.
Every collective method agrees on arguments and deterministic error precedence
before mutation; failed transitions are atomic. Destructors are local. A
transaction weakly references store authority and strongly retains any base
snapshot data it needs.

### T5 implementation gate

Status: **Ready to implement under the approved collective-state contract.**
Independent architecture and adversarial reviews found no additional public or
scientific choice requiring user approval.

`make_state_store` accepts `const StateLayout&` so no potentially allocating
layout copy occurs before the factory's collective failure boundary. A
dimension-erased layout control retains the run fatal policy, immutable mesh
lifetime, run communicator, and authoritative mesh communicator. Factory input
agreement first uses the retained run communicator, allowing layouts from
different meshes in one run to return `replicated_layout_mismatch` before any
rank enters a distinct mesh context. After exact layout agreement, state
transitions use the mesh communicator. Passing layouts from unrelated run
contexts, or crossing stores on unrelated mesh communicator contexts, remains
an MPI caller-ordering precondition because congruent contexts cannot be
portably distinguished across ranks.

`StateStore` is move-constructible but neither copyable nor move-assignable. It
holds a shared stable publication authority. Snapshots and transactions share
an immutable collective context; transactions retain their base data strongly,
refer to mutable store authority weakly, and retain a tombstone containing the
communicator, store identity, and communicator-consistent transaction identity
after move, abandonment, sealing, or store expiry. Consequently, an asymmetric
inactive or expired `seal_collective()` still enters the common agreement and
returns the same structured result rather than dangling or deadlocking.

Every transition begins with one exact collective envelope containing the
operation kind, `StateStoreId`, transaction identity or snapshot argument, and
active/authority flags. The deterministic error precedence is operation,
store, transaction/argument/layout/policy mismatch; expired store; inactive
transaction; unknown snapshot; wrong candidate state or invalid discard; stale
accepted root; then finite identity exhaustion. `StateTransitionError` is a
fixed-size allocation-free value containing this ordered code; its diagnostic
message is a static `string_view`. MPI, dependency, or post-agreement allocation
failures remain fatal through the retained run control.

State, transaction, snapshot, epoch, and level-set identities are selected
collectively from collision-free world-origin/process-global sequences. All
logical validation precedes reservation, and a rejected logical transition
consumes no public identity. Multi-identity reservations are atomic: exhaustion
of one component advances none. Once a reservation succeeds, a later local
allocation failure is fatal and the reserved identity is never reused.

Each snapshot stamp contains complete `SpaceProvenance`, `StateStoreId`,
`StateSnapshotId`, and an optional published `StateEpoch`. Local vector lookup
uses `StateFieldReference`, which carries space provenance, the optional phase
reference, and `FieldGroupId`; wrong layout, phase, or group is rejected before
storage access. Snapshot lookup is immutable. Transaction field access is
owner-only and locally checked; an inactive or expired transaction rejects
access safely. Regional vector mutation/read remains private until T7.

`begin_trial_collective` copies a registered accepted, previous, or retained
private base and records its accepted lineage root. `seal_collective` preserves
the active transaction on every expected failure and consumes it only after a
fully allocated private candidate is registered. Publishing preserves the
candidate snapshot ID, returns a new published view with a `StateEpoch`, rotates
accepted to previous atomically, and leaves an existing private candidate handle
immutable. A sibling whose propagated root is no longer accepted is stale.
Discard removes only registered private metadata; external immutable handles
remain readable. Pinning and explicit retention capacity are T6; synchronized
regional values and representation-exact signed-zero/NaN level-set revision are
T7. T5 preserves only ordinary finite-value revision behavior and makes the
resulting identity collective.

Acceptance requires small serial 2D/3D tests and genuine two- and three-rank
tests for full provenance, disjoint/overlapping store identity, owned vector
partition and global checksums, swapped same-base transactions, crossed
operation/store envelopes, stale lineage, failed-transition atomicity and
no-ID-consumption, private/published/discard states, move and expired-authority
lifetime, and one intermediate-rank divergence. Stage-armed fatal children
cover factory, begin, seal, and publish MPI/allocation failures with the exact
marker-plus-real-abort wrapper used by T4. A deterministic finite-state model
checks short transition sequences. Authoritative guarded LLVM line, definition,
and authored-branch coverage must remain 100 percent; strict raw gcovr remains
published with regenerated checksummed missing-location inventories.

Capacity zero is valid. Pinning is idempotent; exceeding the explicit limit
returns `pin_limit_reached`, with no implicit victim eviction. `unpin` is
idempotent. Accepted, previous, and active-transaction bases do not consume pin
capacity. A pinned private snapshot cannot be discarded; publication removes
its private pin. External immutable handles remain readable after unpinning or
store eviction, but an ID is usable for a new transition only while registered
or pinned by the store. Regional synchronization is collective during writes
and sealing; `regional_value()` is a local read from synchronized immutable
snapshot data.

### T6 implementation gate

Status: **Option A approved by the user on 2026-08-25.** Store-owned retention
contains exactly the accepted snapshot, the immediately previous publication,
at most one unpinned transient private candidate, and at most
`max_pinned_private_snapshots` explicitly pinned private candidates. The
authority therefore owns at most `N + 3` bundles without computing `N + 3` in
a potentially overflowing expression. External handles and active
transactions may intentionally retain additional immutable data outside this
lookup bound.

Successful sealing installs the new candidate as the transient private
snapshot and evicts the former transient candidate. Pinning the transient
candidate converts it to a pinned record and clears the transient slot.
Unpinning a pinned candidate immediately promotes it to the transient slot and
evicts the former transient candidate. Pinning an already pinned record and
unpinning the current transient record are successful no-ops. A stale private
candidate may still be pinned; retention never relaxes its lineage.

Publishing a transient or pinned candidate removes its private retention state,
preserves its snapshot ID, rotates accepted to previous, and frees any pin it
held. A different transient sibling remains registered and becomes stale after
publication. Discard succeeds only for the transient unpinned candidate; a
pinned private candidate, accepted snapshot, or previous publication returns
`invalid_discard`. Pin or unpin on accepted/previous returns
`wrong_candidate_state`; unknown, foreign, or evicted IDs return
`unknown_snapshot`. A new pin at capacity returns `pin_limit_reached`, checked
after the idempotent already-pinned case, and never evicts a victim.

Active transactions strongly retain their base data but do not keep its ID in
the store lookup and consume no pin capacity. A transaction opened before its
base is evicted may still seal; a new begin call for that ID returns
`unknown_snapshot`. Store destruction releases pins locally and performs no
collective transition. Existing external immutable handles remain readable
after unpin, eviction, discard, publication rotation, or store destruction.

The collective operation enum appends `pin` and `unpin` without changing the
existing values. Error precedence is operation, store, snapshot argument,
unknown snapshot, wrong candidate state or invalid discard, pin limit, stale
root for publication, then identity exhaustion for transitions that reserve an
identity. Pin, unpin, and discard reserve none. Their logical checks and exact
collective agreement precede allocation-free mutation; MPI-status failures use
the existing retained fatal route.

Acceptance requires an independent retention model over capacities zero, one,
two, and `SIZE_MAX`, enumerating short seal/pin/unpin/publish/discard/begin
sequences. Focused 2D/3D and true two-/three-rank tests must kill off-by-one,
duplicate-pin, unpin-underflow, failed-pin eviction, forgotten-publish-unpin,
pinned-discard, newest/random/use-count victim, active-base protection,
unbounded-registry, overflow, crossed-operation, and pre-agreement-mutation
mutants. Vector values are read through external handles after every removal
mode. T6 changes no numerical discretization or floating-point behavior. T7
continues to own regional synchronization and representation-exact level-set
revision semantics.

### T7 implementation gate

Status: **Ready to implement under the previously approved T1–T7 public API
contract.** The architecture and adversarial reviews on 2026-08-25 found no
new scientific decision. The refinements below make the already approved
checked lookup, collective-argument, and exact-representation rules executable.

The public spellings remain
`MutableStateTransaction::set_regional_value_collective(RegionalEntryId,
double)` and `StateSnapshot::regional_value(RegionalEntryId) const`.
The setter is collective on the retained mesh communicator; every rank supplies
the same transaction, entry ID, and exact `double` representation. It changes
only transaction-private state, reserves no identity, and leaves the active
transaction byte-for-byte unchanged after an expected failure. The local
snapshot getter is allocation-free, owner-independent, and throws
`std::out_of_range` for an entry outside that snapshot's layout.

Append `unknown_regional_entry` to `StateTransitionErrorCode` and append private
operation value `set_regional_value = 7`, preserving all existing numeric
values. The existing `argument_mismatch` is generalized to every
state-transition argument and covers both divergent regional IDs and divergent
value representations; separate entry/value mismatch codes are rejected as
redundant public taxonomy. Setter precedence is operation, store, transaction,
entry argument, value-bit argument, expired authority, inactive transaction,
then a commonly unknown regional entry. A corrected retry must succeed without
identity drift.

The supported representation is IEEE-754 binary64. Production compares and
communicates `std::bit_cast<std::uint64_t>(value)`, guarded by the corresponding
size and `is_iec559` assertions. Floating equality, hashes, `MPI_DOUBLE`, NaN
classification, or canonicalization are not permitted. Consequently `+0.0`
and `-0.0` differ; identical NaN bits are unchanged; any changed NaN bit is a
change; identical infinities are unchanged; and any changed sign, exponent, or
payload bit is a change. Nonfinite values are stored without validation because
geometry/model admissibility is a later boundary.

Each regional backend remains a size-one distributed vector owned only by mesh
communicator rank zero. Every immutable state bundle also stores one replicated
`uint64_t` cache entry per `RegionalEntryId`. After exact agreement, the setter
updates the owner vector on mesh rank zero and the cache on every rank. Sealing
synchronizes root-owned bits with `MPI_UINT64_T` before comparison and identity
reservation. `regional_value()` reconstructs the exact cached representation
locally. Regional-only changes produce a new state snapshot but preserve the
`LevelSetFieldSetSnapshotId`.

Level-set revision comparison bit-casts every locally owned value, combines the
owner-local unchanged flag through the existing exact boolean all-reduction,
and reserves one communicator-consistent revision only when any bit differs.
Expected revision exhaustion leaves the transaction active and all five
identity sequences unchanged. MPI, dependency, or post-agreement allocation
failures remain fatal through the retained run handler.

Acceptance requires independent bit-pattern oracles, not production helpers:
serial 2D/3D tests for initial/multiple/last-write regional values, unknown
lookup, transaction isolation, signed zero, subnormal/finite/infinite values,
fixed NaN payloads, identity exhaustion, T6 retention/lifetime interaction, and
regional-only revision preservation; and genuine two-/three-rank tests for
owner-independent reads, reversed communicator ownership, middle-rank entry and
value outliers, identical/different NaN payloads, crossed operations/stores,
inactive/expired tombstones, atomicity, no identity consumption, and nonroot
level-set changes. A private oracle must verify both the replicated cache and
rank-zero backend. T7 changes no discretization, DoF ownership, tolerance, or
floating-point arithmetic.

## Dependency-ordered test-first tasks

1. Add explicit `tests/mpi/` two-rank registration, bounded timeouts for
   divergence tests, and LLVM source-based coverage with per-process profiles.
2. Add failing run/graph provenance, communicator-lifetime, nonassignability,
   and cross-run rejection tests; implement shared run control.
3. Add failing callback-order/count/agreement, missing-callback, UTF-8,
   invalid-lookup, orientation, cycle, and exhaustive permutation tests;
   implement deterministic graph validation.
4. Add failing immutable-mesh ownership, communicator, identical-topology but
   different-provenance, and registry-lifetime tests; implement mesh snapshots.
5. Add failing serial/two-rank owner-mask, ghost-FE, multi-component partial
   support, adaptive closure, analytic DoF count, and polynomial reproduction
   tests; implement collective envelope closure and spaces.
6. Add failing collective-error agreement, expired-store, stale-sibling, and
   rank-consistent identity tests; implement the state protocol.
7. Add a small independent retention state model covering capacity zero/one,
   pin/unpin, active bases, publication, previous-state rotation, discard, and
   external handles; implement bounded retention.
8. Add exact per-entry regional and field oracles, one-rank modification,
   signed-zero, infinity, and fixed/different NaN-payload tests; implement
   synchronized values and bitwise revision tracking.
9. Run the independent skeptic audit, contract-level V&V, documentation,
   formatting, raw and adjusted coverage, static/dynamic checks, and final
   quality audit.

Tasks 1–8 are serialized because their CMake, public headers, and state/space
implementation overlap. Each receives its own task branch/worktree and local
commit. Documentation and LLVM coverage run after every API task. No new
dependency is required.
