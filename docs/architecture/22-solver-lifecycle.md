---
title: Solver lifecycle
description: State-machine, ownership, epoch, time-integration, and recovery contracts for a Rift simulation.
---

# Solver lifecycle

Rift's top-level driver is a state machine, not a large physics solver that owns
every deal.II object. Components publish immutable snapshots and typed results;
the driver is the only layer allowed to commit a new mesh, space, geometry,
state, or time level. This makes expensive rebuilds explicit, keeps rollback
possible, and gives failures one owner.

The main services are `MeshStore`, `SpaceRegistry`,
`GeometryTopologyManager`, `LevelSetSystem`, `TemporalGeometryService`,
`WorksetRouter`, `RegionalConstraintSystem`, `StateStore`, `ExecutionBackend`,
`OperatorRegistry`, `PreconditionerService`, `NonlinearDriver`,
`TimeIntegrator`, and `AdaptationManager`. Physics packages supply equation
kernels, interface and boundary laws, admissibility checks, transfer policies,
and an implicit/explicit operator split. They do not own the mesh or persistent
distributed vectors.

## Persistent ownership

`MeshStore` owns the deal.II parallel triangulation and mapping. `SpaceRegistry`
owns all `DoFHandler`s, finite-element collections, constraints, and state
layouts, authoritative vector partitioners, and maximum limiter graphs for one
committed space epoch. `ExecutionBackend` owns evaluator, metric, evaluator
communication-partitioner, and scratch resources and publishes an immutable binding
handle after the `SpaceSnapshot` is finalized. The driver pairs that handle
with the space in the complete operator/snapshot bundle; the space does not own
the backend resource. The level-set
space covers the full background mesh; phase spaces cover their physical and
fictitious envelopes and use `FE_Nothing` outside them.

`StateStore` is the only persistent owner of solution vectors. Each entry has a
semantic key rather than a positional meaning:

```cpp
struct StateKey {
  FieldSetId fields;
  TimeLevel time_level;
  StageId stage;
  StateRole role; // accepted, trial, history, residual, correction
};

struct SnapshotTuple {
  SpaceEpoch space;
  BackendBindingId execution_binding;
  GeometrySnapshotId geometry_snapshot;
  WorksetBundleId spatial_worksets;
  StateSnapshotId state_snapshot;
  std::optional<TemporalGeometrySnapshotId> temporal_geometry;
  std::optional<TemporalWorksetBundleId> temporal_worksets;
  std::optional<GeometryEpoch> published_geometry;
  std::optional<StateEpoch> published_state;
  TimeStageId time_stage;
};
```

Vectors satisfy Rift's distributed block-vector concept, initially backed by
deal.II distributed block vectors. The store controls owned/relevant layouts,
ghost freshness, scratch leases, and atomic replacement. Physics code receives
non-owning views whose lifetime cannot exceed an operation.

## Lifecycle states

A normal simulation advances through these stable states:

1. `configured`: models, finite elements, boundaries, solver policies, and
   transfer policies have been validated.
2. `mesh_ready`: the distributed background mesh and its immutable mapping
   exist.
3. `space_ready`: the full-background level-set space, phase envelopes, active
   finite elements, DoFs, constraints, block layouts, and vector partitioners
   form a committed `SpaceSnapshot`; a matching `StateStore` owns the
   persistent states.
4. `operator_ready`: geometry, worksets, exact backends, and the current
   preconditioner configuration agree on their epochs.
5. `step_open`: the accepted solution has been checkpointed in memory and time
   stages may be attempted.
6. `stage_solving`: trial states and geometries are private to the current time
   stage and nonlinear solve.
7. `step_accepted`: all private stages have completed, the error controller has
   accepted and atomically published one endpoint, and output or adaptation may
   run.
8. `rebuilding`: a shadow adaptation or support-allocation transaction is being
   built; the last accepted state remains live until commit.

Only `step_accepted` is an initial adaptation point. No mesh, active FE index,
DoF redistribution, or persistent state transfer occurs inside a time stage or
nonlinear iteration. A future method may relax that restriction only by
defining how to transfer every stage and restart its nonlinear history.

## Time integration

The complete stage, acceptance, controller, and verification contracts are in
[Time integration](21-time-integration.md); this section summarizes their
lifecycle placement.

The production path uses a stiffly accurate IMEX additive Runge–Kutta method.
Every spatial term declares whether it belongs to the explicit operator,
implicit operator, or a paired split whose sum is the documented semidiscrete
residual. The split is configuration data with a stable hash; it cannot change
halfway through a step. Each implicit stage invokes the safeguarded
quasi-Newton procedure in [Nonlinear coupling](20-nonlinear-coupling.md). Stage states, accepted histories,
and embedded-error data live in `StateStore`, not inside the time-integrator
implementation.

Every stage residual also names an immutable `TemporalGeometrySnapshot` built
from its state, spatial geometry, interface kinematics, and method
coefficients. The service supplies swept-volume and activation terms and must
pass the discrete GCL contract in [temporal geometry and
GCL](06-temporal-geometry-and-gcl.md).

The verification path uses a fully implicit Radau IIA method. It calls the same
spatial kernels and exact backend, but its coupled-stage residual and Jacobian
action add the Radau coefficients. This path provides a high-stability
cross-check for splitting and temporal-order studies; it is not a second set of
physics equations. Both integrators identify precisely which geometry snapshot
is used by each residual call and apply the same topology and band safeguards.

```cpp
StepResult advance_one_step(const AcceptedState &base,
                            const TimeStepRequest &request,
                            const SpaceSnapshot &space,
                            TimeIntegrator &method,
                            OperatorRegistry &operators);
```

Opening a step creates a rollback point. Rejected stages and rejected time
steps discard their trial states, geometry candidates, temporal worksets,
limiter snapshots, Krylov data, and preconditioners; the accepted state, time,
and epochs remain unchanged. The
controller may reduce the step and try again without reconstructing a space
unless the reported status explicitly requires it.

## Snapshot and epoch dependency graph

Unique snapshot ids provide the following rules inside an open attempt;
committed epochs apply the same rules across published bundles. This gives
fast, testable invalidation without relying on call order:

- a mesh change invalidates spaces, geometry, worksets, backend bindings,
  vector layouts, transfers, and all operator caches;
- a space change invalidates geometry validation, worksets, backend bindings,
  vector layouts, linearizations, and preconditioners;
- a backend-binding change invalidates its derived traversal schedules,
  operator contexts, and dependent preconditioners, but not the immutable
  space or backend-neutral worksets;
- a spatial geometry change invalidates cut/interface worksets, dependent
  temporal geometry/bundles, limiter snapshots, geometry coefficients,
  linearizations, and geometry-dependent preconditioners;
- a spatial or temporal workset-bundle change invalidates backend schedules and
  prepared data bound to that bundle;
- a temporal-geometry change invalidates its temporal bundle, limiter snapshot,
  linearization, and any swept-term preconditioner;
- a state change invalidates residuals, tangents, constitutive caches, limiter
  snapshots, and any state-dependent preconditioner;
- a model or limiter-policy revision invalidates its prepared data,
  linearizations, and dependent preconditioners; and
- a time-stage change invalidates stage coefficients and time-dependent
  boundary/source caches, temporal geometry, temporal worksets, and limiter
  snapshots, without introducing another mesh/state epoch.

An object records the epochs on which it depends and validates them at every
public entry point. Geometry-independent regular-cell scheduling, for example,
may survive a geometry change; a cut-cell schedule may not. Dependencies are
declared narrowly enough to preserve useful caches but never inferred from a
pointer still being alive.

## Failure routing and recovery

Operations return typed status plus diagnostic context such as phase, field,
`CellId`, rank, and snapshot tuple. The driver maps outcomes to transitions:

- `trial_rejected` stays in the nonlinear stage and invokes globalization;
- `same_space_geometry_restart` clears nonlinear and linear-solver history and
  restarts from a certified topology-preserving private base without changing
  `SpaceEpoch`;
- `needs_space_rebuild` abandons the open step, returns to the accepted rollback
  point, enters an atomic rebuild transaction, and retries the complete time
  step;
- `unsupported_layout_topology_transition` is terminal in the initial
  implementation and preserves the last accepted diagnostic state;
- `time_step_retry` abandons trials and reopens the step with a smaller size;
- `adaptation_rejected` rolls back the shadow rebuild and continues on the old
  accepted mesh; and
- `fatal` stops collectively after preserving the last consistent diagnostic
  state when possible.

MPI-local errors are reduced to one global outcome before any rank commits a
transition. Commit is always collective and swaps a complete snapshot bundle;
there is no interval in which the state layout is new but an operator binding
is old. The mechanics of this transaction are in
[Adaptivity and transfer](08-adaptivity-and-transfer.md), while backend-specific
rules are in [Execution backend](17-execution-backend.md).

## Verification obligations

Lifecycle tests enumerate every allowed transition and require invalid ones to
fail. Fault injection at geometry build, vector allocation, backend binding,
transfer, and MPI agreement points must leave the previous stable snapshot
usable. IMEX tests verify that explicit plus implicit terms reproduce the
unsplit residual and recover the advertised temporal order. Radau convergence
tests isolate spatial from temporal error and compare against IMEX on smooth
problems. Restart tests cover topology-preserving spatial regrouping with
bijective region lineage and a swept-GCL audit, while true topology events
exercise terminal diagnostic preservation. Other tests
cover band exhaustion, nonlinear failure, and time-step rejection. Identity
tests mutate one dependency at a time and ensure that exactly the required
caches invalidate.
