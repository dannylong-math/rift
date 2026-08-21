---
title: Typed worksets and contribution routing
description: Contract for epoch-stamped local entity ranges passed from geometry and spaces to numerical operators.
---

# Typed worksets and contribution routing

## Context

Passing a few raw deal.II iterator ranges to each physics object separates some
cell traversal from physics, but it does not encode *why* an entity is being
visited. A boundary face, a two-sided material interface, a ghost-band face,
and a global compatibility reduction require different metadata and have
different scatter rules. Treating them as one iterator type would move fragile
classification logic back into hot kernels.

Rift therefore routes contributions through typed worksets. A workset is a
short-lived, non-owning view of homogeneous work items prepared from the
[runtime phase graph](01-phase-graph.md), [discrete spaces](02-discrete-state.md), and
an immutable [geometry snapshot](03-geometry-topology.md).

`WorksetRouter` is the workset construction and cache owner. It borrows phase,
space, spatial-geometry, and where required temporal-geometry snapshots;
selects geometry-specific masks, weights, and masses from the
`SpaceRegistry`-owned maximum limiter graphs; publishes immutable bundles; and
drops them when any stamped dependency changes.
`GeometryTopologyManager` remains the source of classification and quadrature,
while execution backends remain the consumers of local-index and evaluator
views.

## Workset taxonomy

The first implementation distinguishes at least these types:

- `VolumeWorkset`: uncut cells or phase-local cut-volume fragments evaluated by
  a bulk phase operator;
- `InteriorFaceWorkset`: ordinary same-phase interior faces needed by an
  operator or stabilization;
- `DomainBoundaryWorkset`: one-sided pieces of the physical domain boundary;
- `MaterialInterfaceWorkset`: oriented two-sided pieces joining one declared
  phase-graph edge;
- `GeometryFieldVolumeWorkset`: full-background cells for level-set transport
  and geometry-field stabilization;
- `GeometryFieldBoundaryWorkset`: exterior background faces for level-set
  inflow, outflow, and far-field policies, never contact-angle physics;
- `InterfaceKinematicsWorkset`: oriented interface points that route the one
  closure-computed normal speed into geometry transport, without storing that
  state-dependent closure result;
- `GhostFaceWorkset`: faces in a cut-cell extension or ghost-penalty band;
- `PatchWorkset`: macroelements or local patches used by stabilization and
  preconditioning;
- `LimiterWorkset`: deterministic phase-local graph edges, lumped masses, and
  ownership metadata for conservative convex limiting; and
- `GlobalReductionWorkset`: connected-region integrals, mean constraints, and
  other contributions that are not owned by one cell.

One additional type is temporal rather than purely spatial:

- `PhaseActivationWorkset`: swept receiver fragments that bind the existing
  temporal mass/flux term to phase-specific activation state and history.

It is produced only after a `TemporalGeometrySnapshot` exists and therefore
belongs to the separately stamped temporal bundle below.

A future `JunctionWorkset` contains all phases incident on a junction and is
consumed only by a `JunctionOperator`. It is not represented as several
`MaterialInterfaceWorkset`s.

The first three physical categories the user sees remain bulk, physical
boundary, and material interface. The additional types are execution contracts,
not new physical domains.

## Responsibilities

Workset routing must:

- group items by operator, phase or interface identity, field-group schema,
  quadrature rule, and compatible execution shape;
- attach all metadata required to evaluate an item without reclassifying it;
- provide local degree-of-freedom index views for every participating field
  group;
- carry stable boundary/interface orientation and phase identities;
- stamp each workset with `SpaceEpoch` and the unique
  `GeometrySnapshotId` from which it was built;
- define deterministic ownership and exactly-once contribution rules across
  MPI ranks; and
- keep local traversal independent of runtime names and configuration maps.

Worksets do not implicitly fetch mutable state. The execution backend passes a
matching `StateSnapshot` alongside them. This makes the linearization point and
rollback behavior visible at the call boundary.

## Non-responsibilities

A workset does not own the mesh, solution vectors, phase systems, or geometry
reconstruction. It does not choose a numerical flux or material model and does
not decide whether an approximate preconditioner closure may replace an exact
residual closure. It only packages already classified entities for a specific
operator contract.

Operators must not retain workset items, deal.II iterators, quadrature views, or
local-index spans after the call returns unless ownership is explicitly
transferred through an immutable shared object.

## Conceptual C++ API

```cpp
struct WorksetStamp {
  WorksetBundleId bundle;
  SpaceEpoch space;
  GeometrySnapshotId geometry;
};

template <class Item>
class WorksetView {
public:
  WorksetStamp stamp() const;
  std::span<const Item> items() const;
};

struct MaterialInterfaceItem {
  InterfaceId interface;
  PhaseId minus_phase;
  PhaseId plus_phase;
  SpaceQualifiedLocalView minus;
  SpaceQualifiedLocalView plus;
  InterfaceQuadratureView quadrature;
};

struct LimiterEdgeItem {
  LimiterEdgeId edge;
  SpaceQualifiedNode first;
  SpaceQualifiedNode second;
  MacroelementId macroelement;
  RankId owner;
};

struct LimiterWorkset {
  WorksetStamp stamp;
  LimiterGraphId maximum_graph;
  std::span<const LimiterEdgeItem> owned_edges;
  ActiveEdgeMask active;
  LumpedMassView masses;
};

struct WorksetBundle {
  std::vector<VolumeWorkset> volumes;
  std::vector<InteriorFaceWorkset> interior_faces;
  std::vector<DomainBoundaryWorkset> domain_boundaries;
  std::vector<MaterialInterfaceWorkset> material_interfaces;
  std::vector<GeometryFieldVolumeWorkset> geometry_fields;
  std::vector<GeometryFieldBoundaryWorkset> geometry_boundaries;
  std::vector<InterfaceKinematicsWorkset> interface_kinematics;
  std::vector<GhostFaceWorkset> ghost_faces;
  std::vector<PatchWorkset> patches;
  std::vector<LimiterWorkset> limiters;
  std::vector<GlobalReductionWorkset> reductions;
};

WorksetBundle make_worksets(const PhaseGraph &,
                            const SpaceSnapshot &,
                            const GeometrySnapshot &);

struct TemporalWorksetStamp {
  TemporalWorksetBundleId bundle;
  SpaceEpoch space;
  GeometrySnapshotId endpoint_geometry;
  TemporalGeometrySnapshotId temporal_geometry;
};

struct TemporalWorksetBundle {
  TemporalWorksetStamp stamp;
  std::vector<PhaseActivationWorkset> phase_activation;
};

TemporalWorksetBundle make_temporal_worksets(
    const PhaseGraph &,
    const SpaceSnapshot &,
    const GeometrySnapshot &endpoint_geometry,
    const TemporalGeometrySnapshot &);
```

The concrete implementation may use deal.II active-cell iterators internally,
compact cell identifiers, `MatrixFree` batches, or custom cut-fragment records.
Those representations remain behind the typed item API. A phase kernel sees a
`VolumeWorkset`, not a range plus a flag asking whether each cell is cut.

Published structural items are backend-neutral. They use stable cell/entity
descriptors, `SpaceId`-qualified local-index views borrowed from the immutable
space, and geometry-owned quadrature handles. Each execution backend derives
and owns its traversal schedule from a bundle, keyed by
`(WorksetBundleId, BackendBindingId)` (and the temporal bundle id when
applicable). Matrix-free and assembled backends therefore consume one
classification without sharing backend-private batch or evaluator objects.

## Item-specific contracts

A volume item identifies exactly one phase contribution and provides either a
whole-cell quadrature view or an explicit phase fragment. A domain-boundary
item is one-sided and carries a boundary identifier. A material-interface item
always carries both phase-local traces and the normal oriented from minus to
plus. The interface operator computes one two-sided exchange and scatters its
explicit side bundles once; only common conserved transfer is related by the
two opposite outward normals.

A geometry-field volume item is defined everywhere on the background mesh and
never depends on phase `FE_Nothing` support. An interface-kinematics item names
the same interface quadrature record as its material-interface item. The
state-dependent `InterfaceKinematicsView` produced by the one closure is bound
through the matching evaluation context; the structural item neither stores
that result nor permits a second computation of `VΓ`.

Ghost-face and patch items name their stabilization region explicitly so they
cannot create physical cross-interface transport accidentally. A global
reduction item names its connected region and reduction operation, permitting
low-Mach compatibility rows and mean constraints to share the same execution
infrastructure without pretending to be cells.

A limiter item names a same-phase graph edge exactly once, including its two
space-qualified endpoint maps, physical or macroelement lumped masses, and MPI
owner. `LimiterGraphId` names the maximum graph constructed and owned by
`SpaceRegistry` for the `SpaceEpoch`;
candidate geometry snapshots may alter its active mask and weights but cannot
add an unallocated edge. Edge factors, active-set decisions, and admissibility
margins belong to a matching state-dependent `LimiterSnapshot`, not the
structural workset. A phase
activation item additionally names its temporal-geometry snapshot and the
swept-fragment measure used by the existing space-time balance.

## Epochs and lifetime

`SpaceEpoch` protects local indices, finite-element layouts, constraints, and
vector partitioners. `GeometrySnapshotId` protects classification, fragment
quadrature, normals, and connectivity, including private nonlinear
candidates. State is deliberately absent from the structural workset stamp.
An evaluation context pairs a workset with a `StateSnapshotId`, time-stage id,
and model-data revision; state-dependent prepared values and caches use that
separate evaluation identity.

A bundle receives a unique, never-reused `WorksetBundleId` and is valid only
for its complete structural stamp. Geometry may be
rebuilt during a nonlinear solve while the solve-time support envelope keeps
`SpaceEpoch` fixed; the old geometry worksets then become invalid. A
phase-state-only update may reuse the structural worksets but still invalidates
prepared constitutive data. Debug builds check the structural and evaluation
identities at every operator entry; optimized builds preserve the same
precondition when forming the evaluation context.

Worksets are immutable after publication and may be traversed concurrently.
Their lifetime cannot exceed the immutable geometry and space views to which
their items refer. A short-lived evaluation context separately owns or borrows
the matching state view for one operator call.

A temporal bundle is valid only for the exact spatial and temporal identities
in `TemporalWorksetStamp`. Rebuilding any contributing stage geometry,
interface kinematics, time-method data, or endpoint geometry invalidates that
bundle even when the structural spatial worksets remain reusable.

## Failure behavior

Bundle construction fails on an epoch mismatch, missing field group,
unavailable operator, ambiguous ownership, duplicate entity, undeclared phase
contact, or local-index view outside the relevant support envelope. An operator
called with an incompatible workset type or stamp reports a contract violation
before reading state.

Empty worksets are valid for rank-local emptiness and for entities declared but
globally unrealized in the initial accepted configuration. In version 1, a
transition between globally empty and realized is a terminal topology event;
an empty range does not authorize phase birth, death, or new edge realization.

## Contract tests

Tests must verify:

1. duplicate-free coverage of all classified volume and surface entities;
2. deterministic workset grouping and interface orientation;
3. opposite-side access for material interfaces with different field schemas;
4. zero physical cross-interface contribution from ghost and patch worksets;
5. MPI ownership and global-reduction results independent of partitioning;
6. valid rank-local and initially global empty worksets, plus terminal
   rejection of a transition to or from global realization;
7. stale-workset rejection after space or geometry changes, plus independent
   state-cache invalidation without structural workset rebuilding;
8. preservation of local indices across geometry updates within one
   `SpaceEpoch`;
9. invalidation after envelope rebuild or adaptive h-refinement;
10. rejection of a temporal activation bundle after any contributing spatial
    or temporal snapshot changes;
11. deterministic limiter-edge coverage and antisymmetric endpoint ownership;
12. reuse of one backend-neutral bundle by scalar, SIMD, matrix-free, and
    assembled schedules without leaking private batch identities; and
13. compile-time or construction-time rejection when an operator is paired with
    the wrong workset type.

Together, these contracts keep domain discovery in geometry and routing while
leaving numerical kernels focused on their own local equations.
