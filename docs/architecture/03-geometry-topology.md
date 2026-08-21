---
title: Geometry and topology manager
description: Contract for level-set reconstruction, phase classification, support envelopes, connectivity, and cut quadrature.
---

# Geometry and topology manager

## Context

Rift represents material regions on a fixed or adaptively refined background
mesh. The full-background level-set field set belongs to the central [discrete
state](02-discrete-state.md); the geometry and topology manager interprets a
version of that field for the phases declared by the [runtime phase
graph](01-phase-graph.md). This separation lets a nonlinear trial change geometry
without letting a physics kernel mutate mesh classification implicitly.

A geometry snapshot is the single authoritative answer to questions such as
which phase occupies a cell fragment, which pairwise interface crosses a cell,
how that interface is oriented, and which connected regions require global
constraints.

## Responsibilities

The manager must:

- classify background cells and quadrature points by `PhaseId`;
- reconstruct cut-volume fragments and pairwise material-interface pieces;
- build volume, surface, and stabilization quadrature on cut entities;
- use the stable minus/plus orientation from each phase-graph edge;
- compute normals, measures, mappings, and other purely geometrical data;
- determine phase connectivity and connected-component identifiers;
- construct and validate a solve-time support envelope for every phase field
  group;
- identify interior, cut, ghost-band, physical-boundary, and macroelement
  entities needed by execution;
- detect unsupported three-or-more-phase junctions rather than hiding them;
  and
- publish immutable snapshots with a unique `GeometrySnapshotId`, the source
  `LevelSetFieldSetSnapshotId`, and the applicable committed epochs.

The exact encoding of multiple phases in the level-set field set is an
implementation choice. Whatever encoding is selected must yield one
unambiguous `PhaseId` almost everywhere and preserve the declared pairwise edge
identity on reconstructed interfaces.

## Solve-time support envelopes

Before a nonlinear solve begins, the manager predicts an envelope containing
the current occupied region and the background cells through which its
interfaces are allowed to travel during that solve. A configurable neighbor
cushion may be used, but its thickness is a policy rather than an operator
assumption.

The envelope becomes part of the current `SpaceEpoch`. It is immutable for the
solve attempt because the phase-local `DoFHandler`s use a real element inside
the envelope and non-dominating `FE_Nothing` outside it. Trial level-set states
may change occupied fragments and cut quadrature inside the envelope, producing
new private `GeometrySnapshotId`s without changing degree-of-freedom numbering.
Promoting a candidate inside an open nonlinear attempt changes only the private
snapshot id. A new `GeometryEpoch` is published only when the top-level driver
accepts the complete time-step endpoint or another accepted-state transaction.

If any required occupied or cut fragment reaches outside the envelope, the
manager returns `SupportEnvelopeExceeded`. The first implementation abandons
the open time step, rolls back to its accepted checkpoint, expands the envelope
through a rebuild transaction, reserves and then commits a new `SpaceEpoch`,
and retries the complete time step. Silent extrapolation, clipping, or assembly with missing phase
unknowns is forbidden.

## Non-responsibilities

The manager does not own the level-set vector, advance or reinitialize it,
evaluate an EOS, choose boundary or interface fluxes, assemble a residual, or
apply state transfer. It describes geometry; [typed worksets](05-worksets.md)
package that description for operators.

It also does not invent a pairwise law at a triple junction. Until a future
`JunctionOperator` exists, detection of a junction is a clear unsupported
configuration or state transition.

## Conceptual C++ API

```cpp
struct GeometryStamp {
  SpaceEpoch space;
  GeometrySnapshotId snapshot;
  LevelSetFieldSetSnapshotId source_level_sets;
  std::optional<GeometryEpoch> published_epoch;
};

struct GeometryBuildRequest {
  const PhaseGraph &phase_graph;
  LevelSetFieldSetSnapshot level_sets;
  std::span<const SupportEnvelope> envelopes;
  SpaceEpoch space_epoch;
};

class GeometryTopologyManager {
public:
  GeometrySnapshot build(const GeometryBuildRequest &request);
  EnvelopePlan plan_envelopes(const LevelSetFieldSetSnapshot &predictor,
                              const PhaseGraph &graph,
                              const EnvelopePolicy &policy) const;
};

class GeometrySnapshot {
public:
  GeometryStamp stamp() const;
  PhaseClassification classification(CellId) const;
  std::span<const CutFragment> fragments(PhaseId) const;
  std::span<const InterfacePiece> interfaces(InterfaceId) const;
  std::span<const ConnectedRegion> connected_regions(PhaseId) const;
};
```

`GeometrySnapshot` owns or shares all reconstruction and quadrature data needed
for its lifetime. Callers do not keep pointers into temporary reconstruction
buffers. A workset factory may retain a shared immutable snapshot or copy
compact indices and quadrature handles from it.

## Epoch and lifetime rules

`SpaceEpoch` changes when background mesh topology, partitioning, field-group
finite elements, constraints, or support envelopes change. Geometry cannot be
built against a level-set `DoFHandler` from another space epoch.

Every geometry-field candidate and geometry candidate has a unique,
never-reused snapshot identifier, including candidates that are later
rejected. A successful accepted-state geometry commit atomically publishes a
new `StateEpoch` and `GeometryEpoch`; rejection leaves the live epoch pair
unchanged. Changes confined to non-geometrical phase fields produce a new
`StateSnapshotId` without changing `LevelSetFieldSetSnapshotId` and therefore
do not require a geometry rebuild.

A snapshot is immutable and self-consistent: classification, connectivity,
quadrature, normals, and measures all refer to the same snapshot identity and
space epoch.
Parallel assembly may share it read-only. Any derived workset becomes stale
when one of the epochs on which it depends no longer matches.

## Geometrical invariants

Within numerical tolerance, phase fragments partition each represented
background cell without negative measure or unintended overlap. Every
interface piece refers to a declared pairwise edge, and its normal points from
that edge's minus phase to its plus phase. Neighboring reconstructions agree on
shared entity identities.

Connectivity identifiers are deterministic for a fixed snapshot and cover all
represented components. They drive global reductions such as the low-Mach
thermodynamic-pressure compatibility equation; they are not inferred
independently by each phase operator.

## Failure behavior

Geometry construction rejects invalid level-set values, ambiguous phase
labels, non-finite mappings, negative quadrature weights beyond tolerance,
undeclared phase contacts, unsupported junctions, and epoch mismatches. A cut
reconstruction failure includes the background cell, incident phases, and
state/geometry stamps in its diagnostic.

Envelope overflow is recoverable by rollback and space rebuild. A violation of
partition or orientation invariants is not recoverable inside the same solve
and must stop assembly.

## Contract tests

Tests must include:

1. exact or high-order-accurate measures for planar and curved manufactured
   cuts;
2. deterministic minus/plus normals under mesh traversal and MPI repartition;
3. partition-of-volume and shared-interface-measure checks;
4. private moving-contour candidates that remain inside an envelope, receive
   new snapshot ids, and leave accepted geometry and state epochs unchanged;
5. contours that leave an envelope and produce `SupportEnvelopeExceeded`;
6. topology-preserving regrouping with deterministic connected-region lineage,
   plus explicit rejection of split, merge, birth, or death;
7. adaptive h-refinement followed by a valid new `SpaceEpoch` reconstruction;
8. ghost-band and macroelement coverage around arbitrarily small cuts;
9. explicit detection of undeclared contacts and triple junctions; and
10. stale-snapshot rejection after rollback or space replacement.

The resulting snapshot is converted into the operator-specific ranges defined
by the [workset contract](05-worksets.md).
