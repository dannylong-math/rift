---
title: Nonlinear coupling
description: Safeguarded quasi-Newton contracts for moving sharp interfaces in a fixed algebraic space.
---

# Nonlinear coupling

Rift solves a moving-interface nonlinear problem while keeping its algebraic
space fixed for the duration of a nonlinear solve. The interface may move,
cut-cell quadrature may change, and worksets may be regrouped, but the mesh,
active finite elements, DoF numbering, constraints, block layout, and
matrix-free binding do not. This is expressed by two different immutable
snapshots rather than by mutating a solver object in place.

A `SpaceSnapshot` is fixed. A `GeometrySnapshot` is reconstructed from the
current full-background level-set field set and may change between accepted
nonlinear iterates. Exact residual and Jacobian-action calls always name both
snapshots, which prevents an operator from mixing quadrature from one iterate
with state or DoFs from another.

## Snapshot contracts

The fixed snapshot contains at least:

- mesh topology, mapping, MPI partition, and space epoch;
- every phase and field-group `DoFHandler`, active FE index, and constraint;
- the state block layout and partitioners, plus the separately owned matching
  backend binding;
- each phase's active envelope, including its fictitious band; and
- the maximum admissible workset, stabilization graph, and `LimiterGraph`
  inside that envelope.

Phase unknowns are globally numbered and solved on their physical region plus
the fixed fictitious band. Outside that envelope their finite element is
`FE_Nothing`. Every band unknown must receive an extension equation or
stabilization coupling; leaving algebraic unknowns with no support is a model
error. The level set has its own finite-element space on the full background
mesh, so moving the interface does not require activating level-set DoFs.

The geometry snapshot contains:

- level-set coefficients and their source `LevelSetFieldSetSnapshotId`;
- cell classification and phase-side orientation;
- cut-volume and embedded-surface quadrature;
- normals, measures, curvature or reconstructed geometric fields;
- active interface and fictitious-stencil masks; and
- a topology signature and unique `GeometrySnapshotId`.

Geometry objects are values with shared immutable storage. Building a candidate
does not modify the live snapshot. A geometry snapshot is valid only against
the exact space epoch named in its header; that epoch includes the mesh and MPI
partition.

```cpp
struct NonlinearBasePoint {
  SpaceHandle space;
  BackendBindingHandle backend;
  GeometryHandle geometry;
  TemporalGeometryHandle temporal_geometry;
  ConstStateHandle state;
  OptionalLimiterSnapshotHandle limiter;
  TimeStageContext time;
};

GeometryBuildResult build_geometry_candidate(
    const SpaceSnapshot &space,
    ConstLevelSetFieldSetView level_sets,
    const GeometryBuildPolicy &policy);

LinearizationHandle linearize_at(const NonlinearBasePoint &base);
```

## Safeguarded quasi-Newton step

At current nonlinear base point `x_k`, Rift builds spatial geometry
`G_k = G(phi_k)`, constructs its stage-consistent temporal geometry `T_k`,
prepares any configured `LimiterSnapshot L_k`, and evaluates both the base
residual and every exact Jacobian action against
`(SpaceSnapshot, G_k, T_k, L_k)`. The Jacobian is the exact derivative, or declared semismooth generalized
derivative, of that frozen-geometry discrete residual.
It does not differentiate cell classification, cut-quadrature generation,
normal reconstruction, curvature reconstruction, swept-domain reconstruction,
or other operations used to construct `G` and `T`. The assembled reference
Jacobian obeys the same definition; see
[Operator evaluation](16-operator-evaluation.md).

A trial `x_k + alpha delta` carries a trial full-background level-set field set. Rift
builds separate candidate spatial and temporal geometries and, where
configured, a candidate limiter snapshot before evaluating the complete merit
residual. Every candidate state, level-set field set, spatial geometry,
temporal geometry, and limiter object receives a unique snapshot id. A
rejected trial is discarded without changing
the live epochs. A promoted trial atomically installs the matching private
state/spatial-geometry/temporal-geometry/limiter tuple as the next base point
inside the open attempt; it does not
advance the accepted epochs. The next linear solve therefore gets a fresh
frozen-geometry linearization. Quasi-Newton secant
history is retained only when its declared geometry-compatibility predicate
passes; the safe default is to clear it whenever the geometry snapshot changes.

The line search or trust-region controller first checks finite values,
thermodynamic admissibility, the fictitious-band guard, and topology status.
Only then may it compare the nonlinear merit measure. Failure on one rank is
combined across MPI before a candidate can commit.

## Geometry-linearization defect safeguard

Omitting reconstruction and shape derivatives is allowed only while the
resulting model defect is commensurate with the inexact nonlinear forcing
target. `GeometryLinearizationDefectEstimator` compares the valid complete
candidate residual on its own spatial/temporal geometry and limiter with both
the base quasi-Newton affine prediction and a typed frozen-geometry diagnostic:

```text
dmodel = ‖R(xtrial, Gtrial, Ttrial, Ltrial)
           − (Rbase + alpha Jbase delta)‖W.
dshape = ‖R(xtrial, Gtrial, Ttrial, Ltrial)
           − Rdiag_frozen(xtrial; Gbase, Tbase, Ldiag)‖W.
```

The diagnostic `Ldiag` is prepared against the frozen diagnostic geometry, not
reused from either ordinary residual context. Both norms use the same block
scaling as nonlinear convergence. `dshape` estimates the omitted spatial and
temporal geometry response, while `dmodel` also detects ordinary affine-model
error. The configured acceptance rule requires both to stay below declared
fractions of the current forcing tolerance. If the
bound fails, Rift reduces the line-search step or trust radius and clears
geometry-dependent secant data. A merit-reducing candidate may be installed
only as a new base point for immediate geometry and limiter rebuild followed by
relinearization; it cannot be declared converged or reuse the failed model.
Repeated failure
returns `time_step_retry` or a typed request for a future more-consistent shape
linearization.

The estimator, thresholds, residual identities, and decision are recorded in
the nonlinear diagnostics. Tests vary interface displacement independently of
the physical update and also exercise limiter active-set changes under their
semismooth contract. They verify that the shape and total defects decrease at the
expected rate, large omitted-shape effects force refresh or damping, and
convergence is never accepted solely from the frozen geometry model.

`Rdiag_frozen` is available only through `FrozenGeometryModelContext`. That
context records the trial-state/level-set provenance and the deliberately
frozen spatial/temporal geometry provenance, carries a diagnostic-only type
tag, bypasses the ordinary exact-operator entry point, and can never serve as
the candidate residual or acceptance context. Ordinary evaluations continue to
reject a geometry whose source level-set identity does not match the supplied
state.

## Band guard and space rebuild

The active envelope must contain every cell touched by physical volume terms,
cut/interface quadrature, extension equations, and all stabilization stencils
for every accepted geometry. A configurable guard distance reserves additional
layers beyond the currently required stencil. Geometry construction returns a
report containing the minimum remaining guard and the exact offending
`CellId`s, if any.

If a trial leaves the safe part of the envelope, globalization reduces its step
length. If no admissible step is useful, the nonlinear solve returns
`needs_space_rebuild`; it must not change active FE indices or redistribute
DoFs in place. The first implementation abandons the open time step, returns
to its accepted checkpoint, rebuilds a larger envelope through the atomic
transaction in [Adaptivity and transfer](08-adaptivity-and-transfer.md), and
retries the complete time step. This policy avoids transferring stage states,
Newton directions, Krylov bases, or line-search history between different
spaces.

The nominal band width is not itself the safety condition. The invariant is
closure of the actual discretization stencil. Adding a wider flux, ghost
penalty, reconstruction, or constitutive neighborhood must therefore extend the
declared stencil and the guard test.

## Topology events

A topology signature records the interface relationships the nonlinear method
assumes: phase adjacency, connected-component information required by the
model, and permitted intersections. An ordinary change in which cells are cut
does not by itself imply a topology change. Coalescence, breakup, disappearance
of a component, or a change in phase adjacency does.

A changed geometry signature is classified before any candidate is promoted.
Cut-cell membership, workset grouping, or a deterministic connected-component
label may refresh while the continuous physical topology and phase adjacency
remain unchanged. If geometry and temporal-geometry services certify that
continuity, provide a one-to-one `RegionId` correspondence, preserve the global
schema/support/coupling graph, and pass the swept-GCL audit, the result is a
same-space geometry restart: the admissible candidate may become a new private
nonlinear base point, but its linearization, Krylov state, preconditioner, and
quasi-Newton history are cleared.

Any true topology event in the space-time slab—including breakup, coalescence,
component appearance/disappearance, or a change in phase adjacency—is
`unsupported_layout_topology_transition` in the first implementation. It is a
terminal event, not `needs_space_rebuild`, because rebuilding from the same
pre-event checkpoint would simply encounter it again. A future extension must
localize an event at a time boundary, publish a new `SpaceEpoch`, and define a
conservative phase-state and global-scalar lineage/remap before such an event
becomes recoverable.

Contact lines are not supported initially. If an interface reaches a physical
boundary in a way that would require a contact-line law, geometry construction
returns `unsupported_topology_event{contact_line}`. It uses the topology-event
reporting path but cannot be restarted as though a pairwise bulk law covered
the missing codimension-two physics.

## Invalidation and failures

A different `GeometrySnapshotId` or `TemporalGeometrySnapshotId` invalidates
the matching cut, interface, or temporal worksets,
geometric coefficient caches, the current linearization, and
geometry-dependent preconditioners. It does not invalidate DoF numbering or
persistent vectors. Advancing the space epoch invalidates all of those plus
backend bindings and state layouts. An evaluation is rejected unless the
geometry snapshot's recorded source geometry-field id equals the supplied
state view's `LevelSetFieldSetSnapshotId`; unrelated phase-block changes do not
force geometry reconstruction.

Recoverable nonlinear outcomes include `trial_rejected`,
`same_space_geometry_restart`, `needs_space_rebuild`, and `time_step_retry`.
Irrecoverable outcomes include inconsistent orientation, quadrature
construction failure on an otherwise valid cell, an unsupported contact-line,
junction, or layout-changing topology event, or a non-finite committed state.
[Solver lifecycle](22-solver-lifecycle.md) assigns each status to its owner and
defines the allowed transition.

## Verification obligations

Tests move a planar and curved interface across cells while the space epoch
remains constant and verify that only geometry-dependent objects rebuild.
Finite-difference and assembled comparisons hold geometry fixed when checking
`Jv`. Separate globalization tests confirm that trial geometries do not mutate
the base point, rejected trials leave all live epochs unchanged, and a promoted
trial changes the private
state/spatial-geometry/temporal-geometry/limiter base atomically without
publishing an accepted epoch. Band tests exercise the last valid stencil layer
and the first invalid one. Topology tests verify a same-space geometry restart
only for certified topology-preserving regrouping with bijective region lineage
and a swept-GCL audit; breakup and coalescence must report the initial
implementation's terminal layout-transition status. A
boundary intersection test must report the unsupported contact-line status
rather than assembling incomplete terms.
