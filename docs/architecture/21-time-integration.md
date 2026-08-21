---
title: Time integration
description: IMEX-ARK production stages, Radau verification, step limits, acceptance audits, and rollback.
---

# Time integration

`TimeIntegrationCoordinator` advances the complete coupled state. Phase
systems, the level-set system, interface closures, and regional constraints do
not own private clocks or accept their own substeps. The production family is
a stiffly accurate IMEX additive Runge–Kutta method; fully implicit Radau IIA
is the verification family.

The coordinator owns stage equations and controller state, while the central
`StateStore` owns all vectors. Every attempt begins from an accepted checkpoint
and either atomically publishes a complete endpoint or discards all trial
state, spatial geometry, temporal geometry, nonlinear history, and
limiter/preconditioner data. Endpoint acceptance advances the committed state and
geometry epochs exactly once; nonlinear base-point promotion inside the attempt
uses private snapshot ids only.

## Operator partition contract

Every semidiscrete term registers an `OperatorTermDescriptor`:

```cpp
struct OperatorTermDescriptor {
  OperatorTermId id;
  OperatorPartition partition;  // explicit, implicit, or paired
  GeometryTimePolicy geometry_time;
  StiffnessTags stiffness;
  DependencySet dependencies;
};
```

The explicit and implicit terms must sum to the documented unsplit residual on
the same state and geometry history. A paired term declares the two pieces that
must be changed together; for example, a semi-implicit capillary treatment
cannot update its surface force while leaving its energy work or geometry
coupling at an unrelated stage. The finalized split has a stable hash and
cannot change inside a time step.

Residual and Jacobian calls name a `TemporalGeometrySnapshot` in addition to
space, state, and spatial geometry. This supplies swept volumes and the
relative moving-interface contribution required by [temporal geometry and
GCL](06-temporal-geometry-and-gcl.md).

## DAE stage manifold

Pressure constraints, closed-region thermodynamic-pressure rows, gauges, local
interface closure, and any solid volume constraints are algebraic parts of the
stage problem. An implicit stage is successful only when its differential and
algebraic residual blocks satisfy their named tolerances on one consistent
state/geometry snapshot.

Rift does not accept a stage and then repair pressure, composition sums,
interface speed, or region compatibility with an unaccounted post-correction.
When a configured CG limiter participates, the limited operator is part of the
stage residual and exposes its conservation, temporal-order, and tangent
contract as specified in [stabilization and
admissibility](12-stabilization-and-admissibility.md).

## Production IMEX-ARK attempt

```cpp
StepResult try_step(const AcceptedSnapshot &base,
                    TimeStep dt,
                    const ImexArkMethod &method,
                    TimeControllerState controller);
```

For every stage, the coordinator forms explicit history terms, builds the
stage's spatial and temporal geometry candidates, and invokes the safeguarded
quasi-Newton solve for the implicit blocks. Stiff accuracy makes the last stage
the endpoint candidate without a separate inconsistent reconstruction.
Embedded error data remain private until acceptance.

The initial split may put physical advection and a level-set history/predictor
term in the explicit part, while chemistry, multicomponent diffusion, heat
conduction, viscosity, interface kinetics, regional constraints, and
semi-implicit capillarity are implicit. The stage level-set equation and its
dependence on closure-computed `VΓ` remain blocks of the coupled nonlinear
solve; an explicit predictor is only an initial/history contribution. The split
is configuration data constrained by each term's supported partitions, not
hard-coded branching in phase kernels.

## Step-size constraints

`TimeStepConstraintService` gathers named upper bounds and chooses their
minimum with safety factors. Providers include:

- explicit advective/acoustic and entropy-viscosity stability limits;
- the invariant-domain low-order graph bound and its limiting cell or edge;
- any explicitly treated diffusion, chemistry, or capillary limit;
- interface displacement relative to the available support cushion and all
  extension/reconstruction stencils;
- temporal-geometry quadrature validity and proximity to a topology event;
- nonlinear convergence history and local interface-closure robustness; and
- the embedded-error controller's accuracy proposal.

Each bound records its source snapshot, local value, global MPI reduction, and
limiting phase/cell/interface. A missing required provider is a configuration
error. The cushion criterion is derived from predicted displacement and
stencil closure; “two or three layers” is only its nominal minimum.

## Composite acceptance audit

An endpoint is published only if all of these pass:

1. every implicit stage and local closure meets its nonlinear/algebraic
   tolerance;
2. the embedded temporal error satisfies every named block scale;
3. density, temperature, composition, deformation, and model-specific
   admissibility checks pass for the complete limited candidate;
4. phase, species, elemental, momentum, energy, and regional-constraint
   ledgers meet tolerance;
5. the swept-domain GCL and interface-speed identity pass;
6. geometry-linearization defect, support cushion, orientation, and topology
   checks pass; and
7. all exact residual evaluations are finite and share matching snapshot ids.

A global error norm cannot override a failed physical or geometrical audit.
Reinitialization, output-triggered mesh adaptation, and support shrinking occur
only after this acceptance transaction.

## Controller state and rollback

The PI/PID error controller stores accepted step size, accepted error history,
and method order as checkpointed semantic state. A retry computes its proposed
smaller `dt` in private attempt state. Rejecting a step restores the accepted
solution, histories, live epochs, and accepted controller history; only a
diagnostic retry counter persists. Accepting publishes the endpoint and the new
controller state together.

`needs_space_rebuild` first abandons the attempt, then rebuilds from the
accepted checkpoint and retries the complete step. Terminal junction,
contact-line, or layout-changing topology events preserve the last accepted
snapshot for diagnostics.

## Radau IIA verification

The Radau path assembles one coupled-stage residual from the same exact spatial
kernels, constraints, interface closures, and temporal geometry. Its real
Schur or W-transformed linear solver may reuse the same physics
preconditioner services with shifted stage coefficients. It verifies temporal
order, splitting error, stiffness behavior, and conservation; it is not a
fallback that silently changes a failed IMEX step's equations.

## Required tests

Tests cover split-sum equality; advertised IMEX and Radau orders on smooth
manufactured moving-interface problems; stiff chemistry and diffusion limits;
stage satisfaction of all DAE rows; acoustic, capillary, displacement, and
cushion step limits; embedded controller accept/reject sequences; conservation
and GCL at accepted endpoints; MPI-identical limiting-source selection; and
fault injection at every stage and audit boundary. A rejected attempt must
leave the accepted residual bitwise unchanged where deterministic execution is
promised, or within the declared reproducibility tolerance otherwise.

Nonlinear globalization is specified in [nonlinear coupling](20-nonlinear-coupling.md),
and accepted-state rebuilds are specified in [adaptivity and
transfer](08-adaptivity-and-transfer.md).
