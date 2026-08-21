---
title: Discrete state and DoF ownership
description: Contract for central state storage, phase-local field groups, support envelopes, and deal.II spaces.
---

# Discrete state and DoF ownership

## Context

Different phase physics require different unknowns. A fully compressible fluid,
a general-EOS low-Mach fluid, an incompressible mixture, and an Eulerian solid
must not be forced into one padded global finite-element system. Rift instead
uses phase-local field groups while keeping all vector ownership in one central
state store.

Each field group has its own deal.II `DoFHandler`. Examples include a
hydrodynamic group, a species group, or a pressure/constraint group. Splitting
groups permits different finite elements and block treatment without giving
individual physics objects ownership of distributed vectors.

## Responsibilities

The discrete-state layer must:

- define the field groups required by every node in the [runtime phase
  graph](01-phase-graph.md);
- own all current, previous, stage, trial, residual, and update vectors in a
  central state store;
- own one full-background level-set field group, containing all components of
  the configured `LevelSetFieldSet`, used by [geometry and
  topology](03-geometry-topology.md);
- build multiple phase-local `DoFHandler`s when a phase schema contains
  independently managed field groups;
- use a real finite element on a phase's solve-time support envelope and a
  non-dominating `FE_Nothing` outside it;
- apply hanging-node, continuity, boundary, and algebraic constraints at the
  appropriate field-group boundary; and
- construct and own the maximum `LimiterGraph` for each participating field
  group from its support envelope, constraints, and registered limiter
  capability; and
- publish immutable space and state snapshots stamped with `SpaceEpoch` and
  a unique `StateSnapshotId`; only an accepted bundle also carries the live
  `StateEpoch`.

The first implementation uses continuous Galerkin spaces and uniform
polynomial degree within each field group. Adaptive mesh refinement is
supported from version 1, but changing polynomial degree cell by cell is not.
An accepted mesh refinement or coarsening therefore changes the mesh and the
`SpaceEpoch`, while all non-`FE_Nothing` cells of a group still use the same
degree.

## Staged space construction

Space creation has a deliberate draft phase because connected-region rows are
known only after target geometry exists. `SpaceRegistry` first creates a
`SpaceDraft` carrying the provisional `SpaceEpoch`, field-group DoFHandlers,
constraints, field-only partitioners, a `FieldLayoutDraft`, and the maximum
limiter graphs. It contains no backend binding and is not publishable.

During adaptation, `TransferWorkspace` owns provisional field vectors using
that field-only layout. This is where deal.II `SolutionTransfer` interpolates;
the central `StateStore` is not constructed with a partial final layout.
Geometry reconstruction may borrow the draft's level-set space and transferred
level-set buffer. Once `RegionalConstraintSystem` builds the global schema,
`SpaceRegistry` finalizes the complete `StateLayout` and immutable
`SpaceSnapshot`, and `StateStore` adopts the field buffers plus regional
entries. Only then may `ExecutionBackend` build its separately owned binding
from the final space. Initial construction and every rebuild follow the same
ordering.

## Support envelope versus occupied region

A phase's **occupied region** is the current region reconstructed from the
level set. Its **support envelope** is the larger set of background cells on
which that phase's field groups have real degrees of freedom for one nonlinear
solve. The envelope includes the cells in which the interface is permitted to
move during that solve.

The support envelope is immutable from nonlinear initialization through
acceptance or rollback. Consequently, a geometry update inside the envelope
does not redistribute degrees of freedom. If the zero contour exits the
envelope, the solve attempt fails cleanly; the coordinator may enlarge the
envelope, reserve a new provisional `SpaceEpoch`, rebuild spaces and vectors,
commit that epoch, and restart from
a recoverable state. It must never clip the interface motion to preserve the
old layout.

Non-dominating `FE_Nothing` is required outside the envelope so an inactive
cell contributes no unknowns and does not incorrectly dominate hp-interface
constraints against neighboring active elements.

## Non-responsibilities

The state store does not decide which phase occupies a quadrature point, build
cut quadrature, choose material-interface laws, or traverse cells. It exposes
state and local-index views to the [typed worksets](05-worksets.md); numerical
operators cannot retain mutable vector ownership.

The full-background level-set field set is geometrically special but not globally
mutable state hidden inside the geometry manager. It participates in the
coupled nonlinear state and is versioned and rolled back with the other field
groups.

## Conceptual C++ API

```cpp
using FieldGroupId = StrongId<struct FieldGroupTag>;

struct StateSnapshotStamp {
  SpaceEpoch space;
  StateSnapshotId snapshot;
  std::optional<StateEpoch> published_epoch;
};

struct FieldGroupSpace {
  PhaseId phase;
  FieldGroupId group;
  std::shared_ptr<const dealii::DoFHandler<dim>> dof_handler;
  std::shared_ptr<const dealii::AffineConstraints<double>> constraints;
  SpaceEpoch epoch;
};

class StateStore {
public:
  StateSnapshot snapshot(StateSlot) const;
  MutableStateTransaction begin_trial(StateSnapshotId base);
  SpaceEpoch space_epoch() const;
};

class SpaceRegistry {
public:
  const FieldGroupSpace &space(PhaseId, FieldGroupId) const;
  const FieldGroupSpace &level_set_space() const;
  SpaceDraft begin_draft(SpaceEpoch provisional);
  SpaceSnapshot finalize(SpaceDraft &&, const RegionalSchemaSet &);
};
```

`MutableStateTransaction` isolates a nonlinear trial update. Sealing it creates
a new immutable `StateSnapshotId` inside the open attempt; abandoning it drops
the candidate without copying ownership into a phase operator. Only the
top-level driver can publish an accepted endpoint or accepted-state operation,
which advances `StateEpoch`. Geometry rebuilt from changed geometry fields
records their `LevelSetFieldSetSnapshotId` and its own private
`GeometrySnapshotId`.

The `RegionalConstraintSystem` finalizes global entries after combining phase
capabilities, connected regions, and boundary assignments. For example, a
closed low-Mach region adds both one spatially uniform thermodynamic-pressure
unknown and its compatibility row, while an open prescribed-pressure region
adds neither solved entry. The central state store owns every resulting block;
its residuals are produced by reduction worksets rather than ordinary cells.

## Invariants and lifetime

For a fixed `SpaceEpoch`, every field-group `DoFHandler`, finite-element
assignment, constraint matrix, local-to-global numbering, vector partitioner,
and support envelope is immutable. A mesh adaptation, envelope rebuild, field
schema change, or repartition creates a new `SpaceEpoch` and invalidates all
local-index views.

A shadow rebuild receives a unique provisional future `SpaceEpoch` before its
first target object is constructed. It may be validated off to the side but is
not the live epoch until collective commit. A rejected provisional identity is
never reused.

A `StateSnapshot` is read-only and remains valid while its owning state version
is retained. Every trial and accepted snapshot receives a unique,
never-reused `StateSnapshotId`. Geometry-field blocks additionally publish a
`LevelSetFieldSetSnapshotId` that changes only when those blocks change.
`StateEpoch` names only the accepted state revision and changes on top-level
acceptance; promoting or rejecting a nonlinear base point leaves the live epoch
unchanged while private snapshot ids prevent cache aliasing.
A state change does not imply a new space. `GeometryEpoch` is separate because
many state changes do not change cut geometry, while a level-set update does.

Every phase-local real finite element lies inside its recorded support
envelope, and every cell outside that envelope selects non-dominating
`FE_Nothing`. The full-background level-set group never selects `FE_Nothing`.

## Failure behavior

Access with a mismatched phase, field group, vector partition, or epoch is a
contract error and produces a diagnostic before an operator runs. A requested
physics system whose state schema cannot be represented by the configured
field groups fails during initialization.

An interface leaving the support envelope raises a recoverable
support-envelope-exceeded result. Allocation failure, an inconsistent
constraint system, or inability to transfer an accepted state during mesh
adaptation is fatal to the step and triggers rollback.

## Contract tests

Tests must verify that:

1. every configured physics type obtains exactly its declared field groups;
2. multiple groups for one phase receive independent `DoFHandler`s;
3. cells outside an envelope have zero phase-local degrees of freedom;
4. non-dominating `FE_Nothing` produces the intended interface constraints;
5. geometry motion inside an envelope preserves `SpaceEpoch` and numbering;
6. envelope expansion and adaptive h-refinement create a new `SpaceEpoch` and
   transfer retained state conservatively where required;
7. all active cells use the configured uniform polynomial degree;
8. state transactions commit and roll back without stale mutable aliases;
9. the level-set field set remains defined over the complete background mesh;
   and
10. regional schema construction produces the right low-Mach rows, while an
    unsupported layout-changing topology candidate is rejected explicitly.

The [geometry manager](03-geometry-topology.md) consumes the full-background level
set and envelopes, while [workset routing](05-worksets.md) turns the resulting
spaces into short-lived local views.
