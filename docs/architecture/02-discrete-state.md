---
title: Discrete state and DoF ownership
description: Implemented workflow for phase-local deal.II spaces, support envelopes, regional entries, and immutable state snapshots.
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

## Implemented first-stage boundary

The first implementation provides:

- strongly typed identities for field groups, regional entries, spaces,
  complete state snapshots, accepted revisions, and level-set revisions;
- one independently owned `DoFHandler` and constraint matrix for every
  phase-local field group;
- stable support envelopes represented by sets of active `dealii::CellId`
  values;
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
- exact tracking of whether level-set values changed when a trial was sealed.

The first stage closes hanging-node and continuous finite-element constraints.
Boundary constraints, algebraic constraints, limiter graphs, adaptive transfer,
and adopting pre-transferred field buffers remain later implementation stages.
They must extend the finalized layout rather than move vector ownership into a
phase model.

## Complete construction and state workflow

The normal workflow is: build the runtime phase graph, describe field groups,
construct a provisional space draft, add the regional schema, and only then
create state vectors. This complete single-process example uses MPI explicitly,
as production and test code do:

```cpp
#include <deal.II/base/mpi.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <iostream>
#include <memory>
#include <rift/discrete_state.hpp>
#include <utility>

int main(int argc, char **argv)
{
  dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);

  auto graph_result =
      rift::make_phase_graph({{"gas", "compressible"}}, {});
  if (!graph_result) {
    for (const auto &error : graph_result.error())
      std::cerr << error.message << '\n';
    return 1;
  }
  auto graph = std::move(graph_result).value();
  const rift::PhaseId gas = graph.find_phase("gas").value();

  auto mesh = std::make_shared<dealii::Triangulation<2>>();
  dealii::GridGenerator::hyper_cube(*mesh);

  rift::SupportEnvelope gas_support;
  for (const auto &cell : mesh->active_cell_iterators())
    gas_support.insert(cell->id());

  rift::SpaceSpecification specification{
      .phase_fields = {{gas, "flow", 4, 1, gas_support}},
      .level_set = {"level_sets", 1, 1},
  };

  rift::SpaceRegistry<2> registry(mesh, MPI_COMM_SELF);
  auto draft = registry.begin_draft(graph, std::move(specification));
  if (!draft) {
    for (const auto &error : draft.error())
      std::cerr << error.message << '\n';
    return 1;
  }

  auto space = registry.finalize(std::move(draft).value(),
                                 {{"closed_region_pressure"}});
  if (!space) {
    for (const auto &error : space.error())
      std::cerr << error.message << '\n';
    return 1;
  }

  const rift::FieldGroupId flow =
      space->find_field(gas, "flow").value();
  rift::StateStore state(space->layout());
  const auto initial = state.snapshot(rift::StateSlot::accepted);

  auto trial = state.begin_trial(initial.stamp().snapshot);
  trial.field(flow) = 1.0;
  const auto candidate = trial.seal();
  const auto accepted = state.publish(candidate.stamp().snapshot);

  std::cout << "accepted epoch "
            << accepted.stamp().published_epoch->value() << '\n';
}
```

`begin_draft()` and `finalize()` both return `std::expected` results containing
all independently detected validation errors for their stage. Numerical state
mutation begins only after both stages succeed.

## Phase-local spaces and support envelopes

A phase's **occupied region** is the region reconstructed from current
level-set values. Its **support envelope** is the larger, immutable set of
background cells on which a field group has real degrees of freedom during one
nonlinear attempt. The envelope should include every cell into which the
interface is permitted to move before acceptance or rollback.

For a scalar field group, supported cells select `FE_Q`; unsupported cells
select a non-dominating `FE_Nothing`. A multi-component group uses matching
`FESystem` wrappers for both alternatives. Matching the component structure is
important: deal.II's finite-element domination logic cannot safely compare a
vector-valued `FESystem` with a bare scalar `FE_Nothing`.

Every cell identity in the configured envelope must name an active cell on the
registry's mesh. The registry rejects stale, refined-away, or unrelated cell
identities before distributing degrees of freedom. Once accepted, the exact
envelope is retained in `FieldGroupSpace` for inspection and diagnostics.

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
These are immutable views. The space data retains shared ownership of the
background triangulation, so the attached deal.II objects remain valid for the
space snapshot's lifetime.

## Why construction has two stages

Connected-region unknowns are not necessarily known when phase fields are
first constructed. For example, a closed low-Mach region may add a spatially
uniform thermodynamic-pressure unknown after target geometry has been
classified.

`SpaceRegistry::begin_draft()` therefore builds only the field spaces. It
reserves the provisional `SpaceEpoch` before validation starts; a rejected
draft still consumes its identity, preventing stale cached data from aliasing
a later rebuild. `SpaceDraft` is move-only and exposes no `StateLayout`, so it
cannot accidentally initialize a partial `StateStore`.

`SpaceRegistry::finalize()` validates the regional-entry names, sorts them
lexicographically, assigns stable `RegionalEntryId` values, and creates the
complete layout. In this first implementation each regional scalar uses a
one-entry distributed vector whose sole entry is owned by MPI rank zero.
Finalization preserves the draft's `SpaceEpoch`.

Future transfer support will use a separate provisional workspace between
these two stages. It will not weaken the rule that a normal `StateStore` owns a
complete finalized layout.

## Immutable snapshots and mutable transactions

Constructing `StateStore` allocates every field and regional vector from the
layout and publishes a zero-initialized accepted snapshot. A state stamp has
three identities:

- `SpaceEpoch` says how to interpret all vector indices;
- `StateSnapshotId` uniquely names the complete immutable vector bundle; and
- optional `StateEpoch` says that the bundle was published as an accepted
  revision.

`begin_trial(base_id)` copies a retained immutable bundle into transaction-
private vectors. Mutable access is available only through that transaction.
Calling `seal()` transfers the private vectors into a new immutable snapshot
with a new `StateSnapshotId` and no published epoch. The base remains unchanged.
Calling `abandon()`, assigning another transaction, or destroying an unsealed
transaction discards its private vector copy.

Transactions are move-only. Moving one transfers its sole mutable authority
and makes the source inactive. `StateStore` itself is neither copyable nor
movable because active transactions retain a pointer to their owning store;
keeping the store at a stable address makes that lifetime rule explicit.

`publish(candidate_id)` accepts only a private sealed candidate. It preserves
the candidate's `StateSnapshotId`, assigns a new `StateEpoch`, shifts the old
accepted snapshot into the `previous` slot, and installs the candidate as
accepted. Publication does not change `SpaceEpoch`. Private snapshots may
instead remain retained as nonlinear base points or be removed with
`discard()`.

## Level-set revision tracking

Every complete state snapshot also carries a
`LevelSetFieldSetSnapshotId`. When a trial is sealed, Rift compares the locally
owned level-set vector entries exactly with those of its base and combines the
result over the layout communicator. Changing only a phase or regional block
preserves the level-set identity. Changing any level-set entry reserves a new
identity.

This is revision tracking, not a floating-point convergence test. Exact
comparison is appropriate because the question is whether geometry caches may
refer to precisely the same stored field values. Later geometry construction
will stamp its products with this identity and its own `GeometrySnapshotId`.

## Invariants and lifetime

For one `SpaceEpoch`, every field-group finite-element choice, `DoFHandler`,
constraint matrix, numbering, vector partition, and support envelope is
immutable. Rebuilding after mesh adaptation, envelope expansion, schema
change, or repartitioning reserves a new epoch.

Every state snapshot is read-only and remains valid through its shared storage
even after a later candidate is published. Every complete state bundle gets a
unique, never-reused process-local `StateSnapshotId`; only accepted bundles
carry `StateEpoch`. Snapshot and epoch counters use atomic allocation so
independent stores in one process cannot accidentally reuse an identity.

All state vectors have one central authority. Phase physics, interface
operators, geometry code, and worksets may borrow views but cannot retain
mutable ownership.

## Failure behavior

Draft construction reports structured diagnostics for an unknown phase, empty
or duplicate phase-local names, zero component counts, zero polynomial degree,
unknown support cells, and invalid level-set metadata. Regional finalization
similarly reports empty and duplicate regional names.

Resolving a field through the wrong phase throws `std::invalid_argument`.
Unknown field, regional, or snapshot indices throw `std::out_of_range` through
checked lookup. An inactive transaction rejects further mutable access or
sealing with `std::logic_error`. Publishing an already published snapshot and
discarding an accepted or previous snapshot are also logic errors.

An interface leaving its fixed support envelope will become a recoverable
solve-attempt failure in the geometry stage. The coordinator may enlarge the
envelope, reserve a new provisional `SpaceEpoch`, rebuild, and restart. It must
not clip interface motion merely to preserve the old numbering.

## Tests

Thirty-one focused discrete-state test executables exercise the same contracts in
both 2D and 3D. They cover:

1. independent field-group handlers, canonical ordering, lookup, and phase
   checking;
2. support selection, non-dominating `FE_Nothing`, full-background level sets,
   and uniform degree;
3. new epochs on rebuild and validation of every implemented draft and
   regional error;
4. deterministic regional layout and native distributed-vector allocation;
5. initial, accepted, previous, private, published, discarded, and unknown
   snapshot behavior;
6. transaction isolation, abandonment, inactive access, move semantics, and
   regional values; and
7. preservation or advancement of level-set revision identity according to
   the actual stored values.

The complete debug suite passes with AddressSanitizer and
UndefinedBehaviorSanitizer enabled. Instrumented project code in `include/`
and `src/` has 100% line coverage.

The [geometry manager](03-geometry-topology.md) consumes the full-background
level-set space and support envelopes. [Workset routing](05-worksets.md) will
turn the resulting spaces and immutable snapshots into short-lived local
views, while [regional constraints](15-regional-constraints.md) will supply
the finalized regional schema.
