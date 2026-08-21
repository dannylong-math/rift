---
title: Level-set reinitialization
description: Trigger, algorithm, audit, and rollback contracts for reinitializing full-background level-set field sets.
---

# Level-set reinitialization

Rift represents material geometry with a `LevelSetFieldSet` on the entire
background mesh. A two-region configuration may use one signed field; a
multiphase encoding may use several components selected by the geometry
encoding policy. Advection and nonlinear coupling can degrade signed-distance
quality even when the represented interfaces remain accurate.
Reinitialization restores a useful extension away from the interface, but it
must not become an unrecorded interface-motion or volume-correction step.

Reinitialization is allowed only at an accepted state. It is a guarded
geometry operation with its own checkpoint, audit, and rollback.

## Policy composition

Scheduling and numerical reconstruction are independent variability axes:

```cpp
struct ReinitializationPolicy
{
  std::unique_ptr<ReinitializationTrigger> trigger;
  std::unique_ptr<LevelSetReinitializer> algorithm;
  ReinitializationAudit audit;
};
```

`ReinitializationTrigger` decides whether work should run. Initial trigger
implementations are:

- `EveryNSteps`, the simplest deterministic policy;
- `GradientDefectThreshold`, based on a norm of
  `|∇φ| − 1` in a monitored interface band;
- `HybridTrigger`, which reinitializes when either a quality threshold or a
  maximum step interval is reached.

`LevelSetReinitializer` computes a candidate field. Its interface is
algorithm-neutral so Hamilton–Jacobi, fast-marching, closest-point, or later
high-order methods can be compared without changing the solver lifecycle.

## Conceptual interface

```cpp
struct ReinitializationContext
{
  const SpaceSnapshot &space;
  const GeometrySnapshot &geometry;
  ConstLevelSetFieldSetView level_sets;
  StepIndex accepted_step;
};

class ReinitializationTrigger
{
public:
  virtual TriggerDecision evaluate(
    const ReinitializationContext &) const = 0;
};

class LevelSetReinitializer
{
public:
  virtual ReinitializationResult apply(
    const ReinitializationContext &,
    MutableLevelSetFieldSetView candidate) const = 0;
};
```

These virtual calls occur once per accepted-state operation, not at quadrature
points. A compiled registry constructs the selected trigger and algorithm from
runtime configuration.

## Responsibilities

The policy must:

- operate on the full-background level-set space;
- declare which multiphase encoding policies it supports and whether it acts
  componentwise or on the coupled field set;
- leave the accepted field untouched until the candidate passes its audit;
- use a geometry band wide enough to compute its stencil;
- report convergence, work, and quality metrics;
- preserve the orientation convention used by interface edges;
- identify any change in zero-contour topology.

The policy does not transfer phase state, advance physical time, alter phase
DoF support, or add a physical mass flux. If a method deliberately performs a
volume correction, that correction must be a separately named and audited
operation.

## Acceptance audit

After computing a candidate, Rift reconstructs geometry from both the old and
new field sets on the same background mesh. The audit checks every represented
phase region and pairwise interface:

- maximum and integrated displacement of the zero contour;
- phase-volume change at fixed physical time;
- connected-component and interface-edge topology;
- interface area and corresponding surface-energy change;
- normal-vector error and the new signed-distance defect;
- finite values and a lower bound on `|∇φ|` near the contour;
- compatibility with the current candidate-interface cushions.

The default contract requires zero-contour and phase-volume defects to remain
within configured discretization-scaled tolerances. A topology change is a
failure. A future algorithm may request one only through the complete
layout-changing event transaction defined in [extension
points](23-extension-points.md); a reinitializer declaration alone cannot
authorize it.

An audit failure restores the checkpoint and records the reason. It does not
silently accept a lower-order field or modify phase totals.

## Lifecycle and invalidation

A successful reinitialization atomically publishes the candidate level-set
values and reconstructed geometry, incrementing both `StateEpoch` and
`GeometryEpoch`. Quadrature points, normals, curvature, and classification data
may change even when the set of cut cells does not. The commit rebuilds the
`GeometrySnapshot` and every geometry-dependent workset or cache.

It does not increment `SpaceEpoch` unless the reconstructed contour no longer
fits the current support envelope. In that case the reinitialization cannot be
committed in isolation; it requests a full
[adaptation and transfer transaction](08-adaptivity-and-transfer.md).

Reinitialization never runs:

- inside a Newton or quasi-Newton iteration;
- between the residual and Jacobian action of one linearization;
- on a rejected time-stage state;
- concurrently with mesh adaptation or state transfer.

## Required tests

Every trigger must be tested independently from every algorithm. Required
algorithm conformance tests include:

1. Exact preservation of a planar zero contour for every supported encoding.
2. Convergence of signed-distance quality for circles and spheres.
3. Bounded zero-contour, volume, and area errors under repeated application.
4. Preservation of orientation, phase labels, interface ids, and component
   labels, including multiphase cases without newly created junctions.
5. Deterministic results under MPI repartitioning within stated tolerances.
6. Correct rollback after convergence or audit failure.
7. Cache and workset invalidation after a successful operation.
8. No change to any phase state or conservation ledger at fixed time.

The initial default is `GradientDefectThreshold` with the simplest verified
reinitializer. `EveryNSteps` remains the deterministic reference trigger and a
fallback configuration. Other triggers and algorithms are runtime registry
choices, not changes to the architecture.
