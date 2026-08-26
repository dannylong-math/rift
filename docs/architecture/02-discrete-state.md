---
title: Discrete state and DoF ownership
description: Accepted workflow for provenance-safe distributed spaces, support envelopes, regional entries, and immutable state snapshots.
---

# Discrete state and DoF ownership

## Context

Different phase models require different unknowns. A compressible fluid, a
low-Mach fluid, and an Eulerian solid should not be forced into one padded
finite-element system simply because they share a background mesh. Rift gives
each phase-local field group its own deal.II `DoFHandler`, while one central
state store retains the corresponding distributed vectors.

A field group is an independently numbered collection such as flow variables,
species, or a pressure constraint field. A phase may own several groups with
different component counts and polynomial degrees. The geometry-defining
level-set fields form one additional group, but that group is defined over the
entire background mesh rather than one phase's support.

This separation answers two different questions explicitly:

- `SpaceRegistry` decides where degrees of freedom exist and how they are
  numbered for one `SpaceEpoch`.
- `StateStore` decides which immutable values are accepted, previous, or
  private trial data within that layout.

Neither object decides which phase currently occupies a quadrature point.
That belongs to [geometry and topology](03-geometry-topology.md).

## Verified baseline and required refinement

The existing baseline provides, in single-process 2D and 3D tests:

- strongly typed identities for field groups, regional entries, spaces,
  complete state snapshots, accepted revisions, and level-set revisions;
- one independently owned `DoFHandler` and constraint matrix for every
  phase-local field group;
- support envelopes represented by sets of active `dealii::CellId` values;
- a real continuous-Galerkin element inside each envelope and component-
  compatible, non-dominating `FE_Nothing` outside it;
- one full-background level-set group that never selects `FE_Nothing`;
- uniform polynomial degree within each field group, in both 2D and 3D;
- staged construction through `SpaceDraft` followed by regional-layout
  finalization;
- centrally allocated native
  `dealii::LinearAlgebra::distributed::Vector<double>` storage;
- immutable accepted, previous, and private state snapshots;
- copy-isolated, move-only mutable transactions; and
- value-comparison tracking of whether level-set values changed when a trial
  was sealed.

This evidence does not establish distributed or adaptive-mesh conformance. The
completed first stage must additionally provide:

- run, graph, mesh, space, store, and snapshot provenance that is consistent
  across the mesh communicator;
- an immutable mesh snapshot whose communicator and lifetime enclose every
  attached deal.II object and state vector;
- a replicated field schema separated from rank-local, owner-partitioned
  support masks;
- collective schema and envelope validation with the same result on every
  rank;
- monotone hanging-face conformity closure, including closure across MPI
  partitions;
- active-FE assignment only on locally owned cells and verified ghost values
  after deal.II communication;
- explicit collective state-transition semantics, safe transaction/store
  lifetime, stale-base publication rejection, and bounded store retention;
- representation-exact level-set revision tracking; and
- owner-independent access to synchronized regional scalar values.

The completed first stage closes hanging-node and continuous finite-element
constraints only after making every support mask conforming. It must construct
valid spaces on an already adaptively refined distributed mesh. Runtime mesh
adaptation, field transfer, boundary constraints, algebraic constraints,
limiter graphs, and adopting pre-transferred buffers remain later stages. They
must extend the finalized layout rather than move vector ownership into a phase
model.

## Baseline construction and state workflow

The normal staging is: build the runtime phase graph, describe field groups,
construct a provisional space draft, add the regional schema, and only then
create state vectors. The following current single-process pattern illustrates
the provenance and ownership staging without a second communicator argument:

```cpp
#include <deal.II/base/mpi.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <iostream>
#include <memory>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>
#include <utility>
#include <vector>

int main(int argc, char **argv)
{
  dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);

  auto run_result = rift::RunConfiguration::create(MPI_COMM_SELF);
  if (!run_result)
    return 1;
  auto &run = *run_result;
  auto graph_result =
      rift::make_phase_graph(run, {{"gas", "compressible"}}, {});
  if (!graph_result) {
    for (const auto &error : graph_result.error())
      std::cerr << error.message << '\n';
    return 1;
  }
  auto &graph = *graph_result;
  const auto gas =
      graph.reference(graph.find_phase("gas").value()).value();

  auto triangulation = std::make_unique<dealii::Triangulation<2>>();
  dealii::GridGenerator::hyper_cube(*triangulation);
  auto mesh_result =
      rift::make_mesh_snapshot(run, std::move(triangulation));
  if (!mesh_result)
    return 1;
  const auto mesh = *mesh_result;

  rift::SupportEnvelope gas_support;
  for (const auto &cell : mesh->triangulation().active_cell_iterators())
    if (cell->is_locally_owned())
      gas_support.insert(cell->id());

  rift::SpaceSpecification specification{
      .phase_fields = {{gas, "flow", 4, 1}},
      .level_set = {"level_sets", 1, 1},
  };
  std::vector<rift::PhaseSupportSpecification> supports{{
      .phase = gas,
      .mesh = mesh->id(),
      .locally_owned_requested_cells = std::move(gas_support),
  }};

  rift::SpaceRegistry<2> registry(mesh);
  auto draft = registry.begin_draft(
      graph, std::move(specification), std::move(supports));
  if (!draft) {
    for (const auto &error : draft.error())
      std::cerr << error.message << '\n';
    return 1;
  }

  auto space = registry.finalize(*draft, {{"closed_region_pressure"}});
  if (!space) {
    for (const auto &error : space.error())
      std::cerr << error.message << '\n';
    return 1;
  }

  std::cout << "space epoch " << space->epoch().value() << '\n';
}
```

`begin_draft()` and `finalize()` both return `std::expected` results containing
all independently detected validation errors for their stage. Numerical state
mutation begins only after both stages succeed.

Finalization is transactional: it accepts an active draft lvalue, leaves that
draft active after any logical validation error, and consumes it only after a
complete snapshot has been formed. This prevents a rank-asymmetric foreign
draft error from releasing unrelated distributed mesh contexts inside the
target registry's collective operation.

The refined API replaces the externally mutable mesh and separately supplied
communicator with an immutable mesh snapshot carrying a never-reused
`MeshSnapshotId`. It also receives provenance-bearing graph references and
separates the replicated space schema from the owner-local support plan. Graph
and mesh factories receive the approved `const RunConfiguration &` directly.
A mesh snapshot retains shared run control, so it remains valid if the caller
moves or destroys its `RunConfiguration` handle. The provenance and ownership
semantics are fixed.

## Phase-local spaces and support envelopes

A phase's **occupied region** is the region reconstructed from current
level-set values. Its **requested support envelope** is the larger set of
background cells requested by geometry and stencil policies for one nonlinear
attempt. `SpaceRegistry` expands that request to a **closed support envelope**
that is safe for the finite-element layout. The closed envelope is immutable
for the resulting `SpaceEpoch` and includes every cell into which the
interface is permitted to move before acceptance or rollback.

The first implementation uses one closed mask per phase, shared by every field
group belonging to that phase. A future independently supported field group
requires an explicit envelope-group identity and the same closure rules; it
must not silently diverge from the phase mask.

For a scalar field group, supported cells select `FE_Q`; unsupported cells
select a non-dominating `FE_Nothing`. A multi-component group uses matching
`FESystem` wrappers for both alternatives. Matching the component structure is
important: deal.II's finite-element domination logic cannot safely compare a
vector-valued `FESystem` with a bare scalar `FE_Nothing`.

The replicated field schema contains phase, name, component, and polynomial-
degree information and must agree exactly on all ranks. Each rank supplies
only the locally owned active-cell identities in its requested phase masks;
their union is the global requested envelope. Every mask is stamped with graph
and mesh provenance. The registry collectively rejects a stale, refined-away,
unowned, or unrelated cell identity and returns the same deterministically
ordered diagnostics on every rank.

Before distributing DoFs, the registry closes each phase mask monotonically:
if a coarse face meets refined children with mixed real and `FE_Nothing`
selection, every fine child touching that coarse face becomes supported.
Owner masks are published to ghosts, activation requests are sent back to cell
owners, a global changed flag is reduced, and closure repeats to a fixed point.
The registry records requested cells and cells added by conformity separately.

Each field-group `DoFHandler` sets active FE indices only on locally owned
cells. `DoFHandler::distribute_dofs()` communicates those values to ghost
cells; the registry then verifies locally owned and ghost selections and never
queries artificial-cell active FE indices. All groups of one phase receive the
same final mask.

The level-set group follows a different rule. Every active background cell
uses the real element, so level-set values remain available wherever geometry
must be reconstructed. Its `FieldGroupId` follows all phase-local group IDs in
the finalized layout.

## Stable field ordering and lookup

Input field declarations are canonicalized by `(PhaseId, name)`. The registry
then assigns contiguous `FieldGroupId` values in that order. Reordering a
logically identical input specification therefore does not change field IDs.

`SpaceSnapshot::find_field(phase, name)` is the configuration and diagnostic
lookup. It returns no value for an absent name. `field_space(phase, id)` is the
checked identity lookup used after configuration: it rejects an ID belonging
to another phase instead of silently returning the wrong space.

Each returned `FieldGroupSpace` exposes its own `DoFHandler`, closed
constraints, component count, polynomial degree, support envelope, and epoch.
These are immutable views. The space data retains the immutable mesh snapshot,
so the attached deal.II objects and their communicator remain valid for the
space snapshot and every state vector built from its layout.

An immutable mesh snapshot owns the triangulation rather than sharing a
mutable triangulation supplied by application code. Its mesh communicator is
authoritative for space construction and state operations. A surrounding run
communicator may be identical or `MPI_CONGRUENT`; `MPI_SIMILAR` communicators
with reordered ranks and unrelated communicators are rejected. In particular,
rank-zero regional ownership always refers to rank zero of the mesh
communicator.

Every rank participating in mesh-snapshot construction must pass a
triangulation that was constructed collectively on the same mesh-communicator
context. This is a caller precondition of the external-triangulation factory:
process-local `MPI_Comm_compare()` calls cannot portably detect ranks
alternating between two distinct communicator contexts that are each
congruent with the run communicator. Rift diagnoses the detectable
`MPI_IDENT`, `MPI_CONGRUENT`, `MPI_SIMILAR`, and `MPI_UNEQUAL` relationships but
does not claim to repair or diagnose a call that violates this collective MPI
precondition.

Both communicators must be live intracommunicators wholly derived from the same
current `MPI_COMM_WORLD`. Run creation supports world, self, duplicates,
splits/subgroups, and Cartesian or otherwise reordered communicators. The mesh
communicator must additionally compare as identical or congruent with the run
communicator. Intercommunicators and intracommunicators containing dynamic or
MPI Sessions processes that cannot all be translated into that world are
reported as unsupported logical inputs.

## Why construction has two stages

Connected-region unknowns are not necessarily known when phase fields are
first constructed. For example, a closed low-Mach region may add a spatially
uniform thermodynamic-pressure unknown after target geometry has been
classified.

`SpaceRegistry::begin_draft()` therefore builds only the field spaces. It
reserves the provisional `SpaceEpoch` before validation starts; a rejected
draft still consumes its identity, preventing stale cached data from aliasing
a later rebuild. `SpaceDraft` is move-only and exposes no `StateLayout`, so it
cannot accidentally initialize a partial `StateStore`. The draft records its
run, graph, mesh, registry, and provisional-space provenance; finalization by
an unrelated registry or mesh is rejected collectively.

`SpaceRegistry::finalize()` validates the regional-entry names, sorts them
lexicographically, assigns stable `RegionalEntryId` values, and creates the
complete layout. Each regional scalar has one authoritative entry owned by
mesh-communicator rank zero, but this placement is internal. Finalized
snapshots expose a synchronized scalar value on every rank; callers do not
index a rank-zero-only vector. Each immutable state bundle therefore pairs the
owner-only backend vector with a replicated exact-bit cache. Collective writes
require the same entry and `double` representation on every rank, update the
rank-zero backend plus every cache, and reserve no state identity. Local reads
reconstruct the cached representation and reject an unknown entry with
`std::out_of_range`. Finalization preserves the draft's provenance and
`SpaceEpoch`.
Logical regional or foreign-draft errors leave the input draft active for a
corrected retry. A successful finalization makes it inactive; passing an
inactive draft returns `inactive_draft` collectively.

Future transfer support will use a separate provisional workspace between
these two stages. It will not weaken the rule that a normal `StateStore` owns a
complete finalized layout.

## Immutable snapshots and mutable transactions

`make_state_store(layout, retention)` first agrees the layout and policy on the
retained run communicator, then constructs the store collectively on the mesh
communicator. It allocates every field and private regional block from the
layout and publishes a zero-initialized accepted snapshot. The state is a
distributed vector bundle whose semantic
blocks may be backed by separate native deal.II distributed vectors; it
satisfies Rift's block-vector ownership contract without requiring that one
deal.II `BlockVector` object own every independently numbered field space.

A state stamp includes:

- `RunConfigurationId`, `PhaseGraphInstanceId`, and `MeshSnapshotId`;
- `SpaceEpoch` says how to interpret all vector indices;
- communicator-consistent `StateStoreId` identifies the publication authority;
- `StateSnapshotId` uniquely names the complete immutable vector bundle; and
- optional `StateEpoch` says that the bundle was published as an accepted
  revision.

`begin_trial_collective(base_id)` copies a retained immutable bundle into transaction-
private vectors. Mutable access is available only through that transaction.
Calling `seal_collective()` transfers the private vectors into a new immutable snapshot
with a new `StateSnapshotId` and no published epoch. The base remains unchanged.
Calling `abandon()`, assigning another transaction, or destroying an unsealed
transaction discards its private vector copy.

Transactions are move-only. Moving one transfers its sole mutable authority
and makes the source inactive. A transaction holds a weak reference to the
store's control block rather than a raw owner pointer. Destroying the store
makes later transaction access or sealing fail safely and deterministically;
it cannot leave a dangling pointer.

Every private candidate records the accepted-root snapshot from which its
lineage began. `publish_collective(candidate_id)` accepts only a sealed candidate whose
root is still the current accepted snapshot. This compare-and-publish rule
rejects a stale sibling after another candidate has been accepted. Intentional
rollback is a separate future operation, not an override hidden in
`publish_collective()`.

Successful publication preserves the candidate's `StateSnapshotId`, assigns a
new `StateEpoch`, shifts the old accepted snapshot into the `previous` slot,
and installs the candidate as accepted without changing `SpaceEpoch`. The
store lookup retains accepted, previous, one transient unpinned private
candidate, and up to the configured number of pinned private candidates.
Sealing replaces the transient candidate. Pinning converts the transient into
a pinned record; unpinning promotes that record to transient and evicts the
former transient. Publication removes the published candidate's transient or
pin state. An external `StateSnapshot` remains valid through shared immutable
storage after the store stops indexing an older publication, so long runs do
not accumulate every accepted vector bundle in the store map.

Capacity zero permits seal followed directly by publish but rejects a new pin.
Already-pinned and already-transient requests are idempotent. Pin capacity is
orthogonal to lineage: stale private candidates may be pinned but still cannot
publish. A pinned candidate cannot be discarded. Active transactions retain
base data without protecting its lookup ID or consuming pin capacity, and the
authority-owned bound is accepted plus previous plus one transient plus the
configured pins. The implementation handles an unlimited `size_t` capacity
without evaluating an overflowing `capacity + constant` expression.

State construction, trial creation, sealing, pinning, unpinning, publication,
and discarding are collective state transitions. Every rank enters with the
same logical transaction state and receives the same structured result.
Snapshot reads and edits of locally owned field entries are local operations.
Immutable snapshots may be read concurrently, but mutation and publication of
one store are externally serialized.

Store construction first agrees the complete layout and retention policy on
the retained run communicator. Only after that exact agreement may it enter
the authoritative mesh communicator for vector construction and state
transitions. This ordering makes different mesh/layout inputs from one run a
structured mismatch rather than an unmatched collective. Passing layouts from
unrelated run contexts, or crossing stores whose mesh communicators are
unrelated contexts, remains a collective caller-precondition violation that
MPI cannot portably diagnose.

Every transition's first collective descriptor contains the operation kind,
store identity, transaction identity or snapshot argument, and local
active/authority state. The transaction identity distinguishes two trials of
the same base and prevents ranks from sealing them in opposite order into one
mixed candidate. Inactive, sealed, abandoned, moved-from, and expired-store
transactions retain a nonowning tombstone with enough context to participate
in this agreement safely.

Logical rejection is atomic and consumes no state identity or epoch. Identity
reservation occurs only after collective validation, and a reservation bundle
advances no component if any finite sequence is exhausted. Allocation or MPI
failure after successful reservation is fatal; reserved values are never
reused. Collective errors are fixed-size allocation-free values with static
diagnostic text, so returning an agreed error cannot itself allocate
asymmetrically.

Local field-vector access uses a provenance-bearing `StateFieldReference`
rather than a bare `FieldGroupId`. It checks complete space provenance, phase
ownership, and field identity before storage access. This local checked
boundary is distinct from the collective transition protocol.

## Level-set revision tracking

Every complete state snapshot also carries a
`LevelSetFieldSetSnapshotId`. When a trial is sealed, every locally owned
level-set `double` is compared with its base through its exact IEEE-754 binary64
object representation, and the unchanged result is combined over the layout
communicator. Changing only a phase or regional field preserves the level-set
identity. Changing any sign, exponent, or payload bit on any owner rank reserves
a new communicator-consistent identity. Thus `+0.0` and `-0.0` differ, identical
NaN payloads are unchanged, different NaN payloads differ, and identical
infinities are unchanged.

This is revision tracking, not a floating-point convergence or admissibility
test. Nonfinite values remain representable; later geometry/model validation
decides whether they are meaningful. Later geometry construction will stamp
its products with this identity and its own `GeometrySnapshotId`.

## Invariants and lifetime

For one `SpaceEpoch`, the mesh snapshot, partition, graph provenance, every
field-group finite-element choice, `DoFHandler`, constraint matrix, numbering,
vector partition, and closed support envelope are immutable. Rebuilding after
mesh adaptation, envelope expansion, schema change, or repartitioning reserves
a new communicator-consistent epoch.

Every state snapshot is read-only and remains valid through its shared storage
even after a later candidate is published. Run, store, and sequence provenance
make each complete state identity unique and never reused, while every rank
uses the same logical identity. Only accepted bundles carry `StateEpoch`.
Process-global atomic counters are not distributed identity authorities.

All state vectors have one central authority. Phase physics, interface
operators, geometry code, and worksets may borrow views but cannot retain
mutable ownership.

## Failure behavior

Draft construction collectively reports structured diagnostics for graph or
mesh provenance mismatch, replicated-schema mismatch, an unknown phase, empty
or duplicate phase-local names, zero component counts, zero polynomial degree,
unknown or non-owned support cells, nonconvergent envelope closure, communicator
mismatch, and invalid level-set metadata. Regional finalization similarly
reports empty, duplicate, or rank-inconsistent regional schemas.

Local checked lookups report wrong provenance, wrong phase, and unknown field,
regional, or snapshot identities without accessing storage. Collective state
transitions return the same structured failure on every rank for an inactive
or expired transaction, mismatched candidate, stale accepted root, attempted
republication, or invalid discard. MPI communicator failure remains fatal;
one rank must not continue after a collective transition fails elsewhere.

In particular, a non-success MPI return while duplicating or releasing a
communicator, translating groups/ranks, or performing collective agreement is
fatal rather than a recoverable state error. Successfully agreed logical
conditions—unsupported communicator kind or provenance, stale provenance, and
finite identifier-space exhaustion—remain structured expected errors.

An interface leaving its fixed support envelope will become a recoverable
solve-attempt failure in the geometry stage. The coordinator may enlarge the
envelope, reserve a new provisional `SpaceEpoch`, rebuild, and restart. It must
not clip interface motion merely to preserve the old numbering.

## Verification obligations

The existing focused single-process test executables exercise the baseline in
both 2D and 3D. They cover:

1. independent field-group handlers, canonical ordering, lookup, and phase
   checking;
2. support selection, non-dominating `FE_Nothing`, full-background level sets,
   and uniform degree;
3. new epochs on rebuild and validation of every implemented draft and
   regional error;
4. deterministic regional layout and native distributed-vector allocation;
5. initial, accepted, previous, private, published, discarded, and unknown
   snapshot behavior under the baseline retention policy;
6. transaction isolation, abandonment, inactive access, and move semantics; and
7. preservation or advancement of level-set revision identity according to
   the actual stored values.

Completion additionally requires explicit two-rank 2D and 3D tests on a
`parallel::distributed::Triangulation`, using both supported MPI
implementations in the project matrix. Tests must cover serial/partitioned
envelope equivalence, collective schema rejection, owner-only active-FE
assignment, ghost observations, multi-component partial support, adaptive
coarse/fine closure including an MPI-boundary case, constant and linear
polynomial reproduction through hanging constraints, communicator and mesh
provenance, rank-consistent identities, one-rank ordinary finite level-set
changes, transaction/store lifetime, and stale sibling candidates. T6 adds
bounded publication retention. T7 separately tests signed zero, NaN payloads,
and synchronized regional values on owner and nonowner ranks.

Unit tests are an entry condition for contract-level V&V. Analytic DoF counts,
polynomial reproduction, graph/schema permutation invariance, and serial/two-
rank equivalence provide independent oracles. Physical-data validation,
solution verification, and uncertainty quantification are not applicable
until a numerical model is implemented.

The stage is not complete until the guarded exact LLVM metrics for distinct
physical lines, canonical source definitions, and exact canonical authored
branch outcomes are all 100 percent. The coverage-only whole-archive CTest and
reviewed supported 2D/3D template-use manifest are required completeness
conditions. MPI coverage output must be isolated per process before profiles
are merged. The conservative LCOV projection is reported separately. Strict
raw no-exclusion gcovr output remains a mandatory published, non-gating
compiler-CFG diagnostic; no source exclusion or suppression is approved.

The [geometry manager](03-geometry-topology.md) consumes the full-background
level-set space and support envelopes. [Workset routing](05-worksets.md) will
turn the resulting spaces and immutable snapshots into short-lived local
views, while [regional constraints](15-regional-constraints.md) will supply
the finalized regional schema.
