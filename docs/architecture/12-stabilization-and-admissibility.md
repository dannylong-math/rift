---
title: Stabilization and admissibility
description: Entropy viscosity, invariant-domain low-order operators, convex limiting, and nonlinear active-set contracts.
---

# Stabilization and admissibility

Continuous Galerkin transport needs separate answers to two questions:
stabilization controls unresolved oscillations, while admissibility prevents
states such as negative partial density, internal energy, or temperature.
Entropy viscosity helps with the first question but does not prove the second.
Rift therefore gives the CG discretization an explicit
`StabilizationAdmissibilityPolicy` rather than hiding clipping in a material
model or time integrator.

## Policy composition

```cpp
template<class EntropyViscosity,
         class LowOrderGraphOperator,
         class PairwiseConvexLimiter,
         class AdmissibilityOracle>
struct StabilizationAdmissibilityPolicy;
```

The policy type is part of the compiled phase-kernel registry. Runtime data
contains constants, tolerance scales, protected species, and model-specific
admissible-set parameters. The policy declares which PDE state families,
finite elements, scalar types, and time partitions it supports.

The `AdmissibilityOracle` uses the same EOS and state-recovery policy as the
exact phase residual. It checks partial densities, total density, composition
constraints, recoverable internal energy/enthalpy, temperature, deformation
determinant, and any model-specific branch condition. It returns margins and a
typed failure; it never clips a value.

## Entropy-viscosity contract

Entropy viscosity contributes a phase-restricted artificial diffusive flux in
volume and same-phase interior-face worksets. Its entropy residual includes the
configured physical diffusion and chemistry terms so a smooth reacting
solution does not look like a shock merely because entropy is produced
physically.

The coefficient is capped by a declared first-order graph-viscosity scale. It
is computed from one state/geometry snapshot, is scalar/SIMD consistent, and
does not transport mass, species, momentum, or energy across a material
interface. A preconditioner may lag the coefficient; the exact operator may not
use a different sensor between residual and Jacobian action.

## Invariant-domain low-order operator

For every transport-capable phase, `LowOrderGraphOperator` constructs a
mass-lumped, conservative graph update on the phase's physical and cut
macroelement support. A maximum `LimiterGraph` is fixed in the
`SpaceSnapshot`; candidate geometries may change active-edge masks, fragment
weights, and masses but cannot introduce an endpoint pair outside that graph.
Pairwise edge contributions are antisymmetric, preserve constant states,
include boundary/interface flux bounds exactly once, and have a time-step or
nonlinear update condition under which the configured admissible set is
invariant.

Small cut fragments do not define isolated tiny graph masses. The geometry
service supplies macroelement/patch ownership, and the low-order policy states
how cut contributions are aggregated or redistributed while retaining global
and declared macroelement-local conservation. Ghost penalties condition the
extension space but are not counted as physical graph transport.

`LimiterWorkset`s carry deterministic graph edges, lumped masses, physical
fragment measures, boundary/interface ownership, and local field maps. The
graph is never discovered inside an EOS call.

The graph is built on independent or master constrained degrees of freedom;
subsequent constraint distribution must preserve the convex admissible set and
the pairwise conservation ledger. Endpoint identity includes `SpaceId` because
global DoF numbers from different `DoFHandler`s can overlap. Supported
finite-element, quadrature, and macroelement combinations must prove positive
lumped measures. The backend supplies any limiter-specific expanded ghost set
and canonical edge ownership required for race-free exactly-once evaluation.

## Pairwise convex limiting

The high-order CG/entropy-viscosity update is written as the admissible
low-order update plus conservative pairwise antidiffusive corrections. The
limiter chooses one symmetric factor per graph edge so both endpoints receive
opposite corrections and every tested state stays inside the EOS-aware
admissible set. Composition constraints and conserved totals are enforced
together; limiting each species independently is not conforming.

For a nonconvex real-fluid admissible set, the policy performs a scalar search
from the known admissible low-order state toward the candidate, querying the
same EOS oracle at every trial. A failed search rejects the stage or reduces
the nonlinear step. It does not substitute a perfect-gas bound or set a
negative species to zero afterward.

Both exact backends must expose the same explicit pairwise antidiffusive-flux
decomposition; a residual vector alone does not define it uniquely. Irregular
graph traversal may use backend adjacency, coloring, or thread-local
accumulation rather than a regular deal.II cell loop, but those choices cannot
change edge ownership or factors.

## Implicit and nonlinear contract

When limiting participates in an implicit stage, the limited operator is the
documented nonlinear residual. For one complete evaluation identity,
`LimiterEvaluator` creates an immutable `LimiterSnapshot` containing edge
factors, active bounds, admissibility margins, and tangent metadata. Its stamp
contains `LimiterGraphId`, the workset stamp, `StateSnapshotId`, optional
`TemporalGeometrySnapshotId`, time stage, model-data revision, and
limiter-policy revision. Every object receives a unique, never-reused
`LimiterSnapshotId`, but it remains derived cache data rather than a separately
accepted state or epoch.

A linearization pins one `LimiterSnapshotId`. The exact Jacobian action uses a
differentiated limiter or a declared semismooth generalized derivative of the
documented limited residual; a frozen-active-set derivative may pin the branch
selection but must still differentiate the factor formula within that branch.
The assembled reference follows the identical choice. Candidate merit
evaluation constructs a new limiter snapshot, and an active-set change may
force damping and relinearization rather than silent reuse. Holding the numeric
edge factors fixed is an approximate preconditioner operation, not the exact
Jacobian contract.

Globalization tests the complete limited candidate. An active-set change may
force relinearization or a smaller trust region, but it cannot be applied as an
unrecorded post-stage correction. Algebraic pressure/region constraints remain
part of the same stage manifold, and the conservation/GCL audits run after the
limited residual converges.

Explicit stages use the same low/high decomposition under the method's stated
SSP or step-size condition. The time-step constraint service includes the
low-order graph bound and identifies its limiting cell/edge.

## Exact, transfer, and preconditioner boundaries

Mesh/state transfer has its own conservative admissibility correction and does
not reuse a time-stage limiter state. Reinitialization never invokes a phase
limiter. A preconditioner may use the low-order graph operator, frozen limiter
factors, or a smoother surrogate, but the approximation is labeled and cannot
replace the exact limited residual.

The policy owns no vectors or mesh iterators. `SpaceRegistry` constructs and
owns the maximum graph from the phase capability, support envelope, and
constraints. `WorksetRouter` owns geometry-specific masks, weights, and masses;
`StateStore` leases semantically tagged low-order, high-order, and
limited-candidate scratch vectors for one evaluation; the execution backend
owns gather/scatter and MPI synchronization. Releasing a rejected candidate
also releases or invalidates its limiter snapshot and scratch leases. There is
no persistent "accepted limiter vector" to restore independently of the
accepted state.

## Required tests

1. Constant-state, positive-lumped-measure, constraint-prolongation, and
   pairwise antisymmetry tests on regular, cut, hanging-face, and MPI-partition
   graphs.
2. Positivity/admissibility for density, every present species, internal
   energy/enthalpy, temperature, and solid deformation variables.
3. Exact conservation of mass, species, elements, momentum, and total energy
   under limiting, including unequal-species interfaces.
4. Smooth-solution convergence showing inactive limiting and vanishing
   entropy viscosity at the designed rate.
5. Shock and strong-reaction tests that distinguish physical entropy production
   from the numerical entropy residual.
6. Real-EOS scalar-search tests across branch and nonconvex admissibility
   boundaries.
7. Semismooth active-branch Jacobian actions against directional residual
   differences away from and across active-set changes.
8. Scalar/SIMD and assembled/matrix-free agreement for the explicit pairwise
   flux decomposition and a fixed limiter snapshot, including expanded ghost
   neighborhoods.
9. Rollback tests proving rejected candidates do not mutate the nonlinear-base
   limiter snapshot, graph, or phase state.
10. A deliberately impossible admissibility/conservation case that fails
    explicitly instead of clipping or falling back to first order silently.

The composite endpoint checks are part of [time integration](21-time-integration.md),
and mesh-change admissibility is part of [adaptivity and
transfer](08-adaptivity-and-transfer.md).
