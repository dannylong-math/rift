---
title: Software architecture
description: Component boundaries, dependency rules, and shared invariants for Rift's sharp-interface multiphysics solver.
---

# Software architecture

Rift is organized around one rule: physics operators evaluate contributions on
work supplied to them; they do not discover which cells, faces, or interface
fragments belong to a phase. This separates high-performance local physics
from moving-domain classification and makes the same operators usable by
assembled and matrix-free execution paths.

The architecture supports a runtime graph of named phases. Each phase selects
one physics family for an entire run, while its physical domain and algebraic
support move as interfaces advect or undergo phase change. Pairwise material
interfaces are implemented first. Triple junctions are reserved as a distinct
lower-dimensional extension rather than encoded as special cases inside a
pairwise law.

## Reading order

The numbered filenames and Sourcey navigation are the recommended reading
order. The guide progresses through five layers:

1. `00–02`: overview, runtime phase graph, and discrete ownership;
2. `03–09`: spatial geometry, level sets, work routing, temporal geometry,
   activation, adaptation, and reinitialization;
3. `10–15`: phase/model policies, stabilization, boundary and interface laws,
   and regional constraints;
4. `16–22`: exact operator evaluation, execution, linear algebra,
   preconditioning, nonlinear coupling, time integration, and lifecycle; and
5. `23–24`: extension contracts and the living decision ledger.

Read straight through for the complete design. For implementation work, start
with `00–05`, then follow links from the component being implemented; each page
states its own invariants, failure behavior, and required tests.

## Component map

```text
Simulation configuration
  |-- compiled model and operator registries
  |-- runtime PhaseGraph
  |     |-- PhaseSystem nodes
  |     `-- InterfaceOperator edges
  |
  |-- GeometryTopologyManager
  |     |-- phase support and connectivity
  |     `-- cut reconstruction and quadrature records
  |
  |-- LevelSetSystem
  |     |-- full-background geometry-field transport
  |     `-- interface-speed extension and stabilization
  |
  |-- TemporalGeometryService
  |     `-- swept volumes, stage quadrature, and GCL
  |
  |-- WorksetRouter
  |     |-- structural spatial worksets
  |     `-- temporal activation worksets
  |
  |-- RegionalConstraintSystem
  |     `-- global rows, gauges, and nullspace descriptors
  |
  |-- StateStore
  |     |-- accepted state and rollback checkpoint
  |     |-- time-stage and nonlinear vectors
  |     `-- phase, geometry, and global-scalar block views
  |
  `-- CoupledSolver
        |-- IMEX-ARK production integrator
        |-- safeguarded quasi-Newton iteration
        |-- exact residual and Jacobian actions
        `-- preconditioner and diagnostic services
```

The [runtime phase graph](01-phase-graph.md) describes physical adjacency. The
[geometry and topology manager](03-geometry-topology.md) turns the current
level-set field set and graph into immutable snapshots. The [level-set system](04-level-set-system.md)
contributes geometry-field transport, and [temporal geometry and
GCL](06-temporal-geometry-and-gcl.md) supplies conservative swept-domain data. The
[workset contract](05-worksets.md) routes these snapshots to phase, boundary,
interface, geometry-field, stabilization, and global-reduction operators.

## Principal components

### Geometry and topology

`MeshStore` owns the shared `parallel::distributed::Triangulation` and
background mapping, while `SpaceRegistry` owns the full-background level-set
space, every phase-local finite-element space, their layouts, and maximum
limiter graphs. `ExecutionBackend` separately owns evaluator resources and
publishes a binding only after the space is final. `GeometryTopologyManager`
borrows immutable snapshots of those objects and owns phase classification,
connected components, cut quadrature, candidate interface bands, and topology
events. It produces versioned `GeometrySnapshot` objects as the trial level
set changes; the driver supplies the fixed `SpaceSnapshot` for the nonlinear
attempt.

It does not evaluate an equation of state, choose a boundary law, or assemble
a physical residual.

### Level-set and temporal geometry

`LevelSetSystem` evaluates the globally coupled, full-background geometry-field
transport and consumes the one interface speed computed by each material
closure. `TemporalGeometryService` supplies swept volumes and stage-consistent
interface quadrature so the moving-domain residual satisfies a discrete
geometric conservation law. Neither service owns state vectors or reconstructs
phase physics independently.

[Phase activation](07-phase-activation.md) supplies the conservative swept-region
state and history contract, including stress-free fluid-solid transformation
data, without adding a second mass or energy source.

`RegionalConstraintSystem` turns connected-component identities, phase
capabilities, and exterior-boundary assignments into global unknowns,
compatibility rows, gauges, and nullspace descriptors. See [regional
constraints](15-regional-constraints.md).

### Phase systems

A `PhaseSystem` is the runtime facade for one named phase. It combines an
immutable phase definition, a changing support description, multiple
field-group DoFHandlers, typed phase operators, transfer policies, and views
into the central state. It is not an independently advancing time solver.

Concrete phase operators cover fully compressible flow, general-EOS low-Mach
flow, strict or quasi-incompressible flow, and Eulerian thermomechanical
solids. Their hot kernels are statically typed; runtime dispatch occurs once
per homogeneous workset. See [phase systems](10-phase-systems.md) and
[model policies](11-model-policies.md). Continuous-Galerkin transport also uses an
explicit [stabilization and admissibility](12-stabilization-and-admissibility.md)
policy; entropy viscosity is not a substitute for conservative invariant-domain
limiting.

### Boundaries and material interfaces

A [domain-boundary operator](13-boundary-operators.md) is one-sided and is keyed
by a phase and exterior boundary identifier. A
[material-interface operator](14-interface-operators.md) is two-sided. It obtains
canonical physical traces from both phases, solves one local interface
closure, and scatters a common oriented exchange to the two residuals.

Neither side independently recomputes the interface flux. This is the central
conservation rule for unequal species sets, phase change, capillarity, thermal
resistance, and mixed physics pairings.

### Evaluation and linear algebra

The [execution backend](17-execution-backend.md) owns deal.II iterators,
evaluators, gathering, scattering, MPI exchange, and CPU SIMD batching. Local
physics kernels see Rift-defined state, geometry, trace, and contribution
views instead of deal.II types.

The [operator-evaluation contract](16-operator-evaluation.md) provides an exact
matrix-free residual, an exact matrix-free Jacobian action, and an exact
assembled reference implementation. Approximate low-order, patch, coarse, or
mixed-precision operators belong only to preconditioning.

The [central linear-algebra store](18-linear-algebra.md) uses deal.II distributed
block vectors initially and does not require PETSc or Trilinos. Persistent
state precision, evaluation scalar type, and auxiliary-preconditioner
precision are separate template roles. [Preconditioning services](19-preconditioning.md)
compose native matrix-free multigrid, local physics solves, nullspace handling,
and cut-patch corrections without altering the exact operator.

The coupled [time-integration contract](21-time-integration.md) owns the
IMEX-ARK/Radau stage equations, DAE-manifold acceptance, step-limit aggregation,
composite audits, and controller rollback.

## Dependency rules

The following rules are normative:

1. Physics and model kernels must not classify cells or retain mesh iterators.
2. A nonlinear attempt must not change the mesh, active FE indices, DoF
   numbering, block layout, or candidate coupling graph.
3. A residual and its Jacobian action must consume the same immutable space,
   backend binding, spatial/temporal worksets, spatial geometry, temporal
   geometry, state, prepared limiter, time-stage, and model identities.
4. Interface exchange is computed once per interface quadrature point and
   enters adjacent phase residuals with the declared orientation.
5. Runtime selection must not introduce virtual dispatch, allocation, or
   mutable shared scratch inside quadrature loops.
6. Approximate preconditioner physics must never alter the exact nonlinear
   residual.
7. Mesh adaptation, support-envelope changes, and active-FE changes occur only through a
   rollback-capable transaction at an accepted state.
8. Unsupported contact-line or junction creation is reported as a terminal
   topology error; it is never silently reduced to a pairwise interface.

## Epochs and invalidation

Rift uses explicit versioning rather than relying on pointer identity or an
unchanged list of cut cells:

| Epoch | Changes when | Invalidates |
| --- | --- | --- |
| `SpaceEpoch` | Mesh, partition, background mapping, FE support, degree, constraints, or block layout changes | DoF indices, workset structure, sparsity, matrix-free metadata, transfer maps |
| `GeometryEpoch` | A geometry snapshot is published in an accepted bundle | Classification, cut quadrature, normals, curvature, interface masks, geometry coefficients |
| `StateEpoch` | A state snapshot is published in an accepted bundle | Thermodynamic, transport, chemistry, constitutive, interface, and linearization caches |

A cached object declares the epochs and private snapshot ids on which it
depends. An identity mismatch is an error, not a request to use stale data.

Every shadow rebuild reserves a unique provisional `SpaceEpoch` before target
DoFs, worksets, vectors, or backend bindings are constructed. Commit makes that
identity live; rejection never reuses it and does not change the live accepted
epoch.

The background mapping is immutable within a `SpaceEpoch`; changing it rebuilds
every `FEValues` and `MatrixFree` binding. Candidate state and geometry
snapshots also receive unique, never-reused snapshot identifiers. Rejected
candidates do not become the live epoch, but their identifiers prevent caches
from aliasing two different trials based on the same accepted state.

## First implementation boundary

The first implementation uses continuous Galerkin spaces, adaptive
background-mesh refinement with uniform polynomial degree, MPI plus CPU SIMD,
a full-background level-set field set, pairwise interfaces separated from exterior
boundaries, and a safeguarded quasi-Newton geometry coupling. DG, cellwise
`hp`, GPU execution, contact lines, triple junctions, dynamic surface PDEs,
PETSc, and Trilinos remain explicit extension points.

The current status of every architectural choice is summarized in the
[decision ledger](24-decision-status.md).
