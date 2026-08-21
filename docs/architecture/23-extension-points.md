---
title: Extension points
description: Conformance requirements for adding physics, models, interfaces, discretizations, and execution backends.
---

# Extension points

Rift is extensible through explicit registries and concepts, not through
conditional logic added to central assembly loops. An extension is complete
only when it declares its data dependencies, linearization behavior,
admissibility rules, transfer policy, diagnostics, and conformance tests.

## Add a phase physics family

A new physics family must provide:

- an immutable phase-definition type and runtime registry entry;
- a field-group schema and block metadata;
- compatible finite-element descriptions for the supported discretizations;
- volume, source, stabilization, and trace-adapter kernels;
- a conforming `StabilizationAdmissibilityPolicy` for transported CG fields,
  including its exact tangent choice;
- a `PhaseActivationPolicy` for every permitted moving/transformation entry
  into physical fragments, or an explicit capability proving that such entry
  cannot occur;
- boundary-exchange and interface-exchange accumulation adapters;
- exact residual and Jacobian-action support;
- admissibility and nonlinear-step checks;
- a physics-aware mesh/support transfer policy;
- preconditioner block descriptions and approximate-operator builders;
- manufactured, conservation, transfer, activation-history, and limit tests.

The new family may not classify its own cells, advance time independently, or
introduce a private primary-state store. See [phase systems](10-phase-systems.md).

## Add a constitutive or reaction model

A new EOS, transport, chemistry, or solid constitutive policy must satisfy the
[model-policy contract](11-model-policies.md). It is registered as one of a
bounded set of compiled types while its mechanism and coefficient data may be
loaded at runtime.

The policy must support the required `EvaluationScalar` types, explicit
scratch, deterministic failure reporting, admissibility checks, and tangents.
If an external library cannot support SIMD or AD, its adapter must declare the
fallback and batching behavior; it may not hide per-point virtual dispatch or
shared mutable state.

A future mechanism-generation step may emit a new compiled policy and registry
entry without changing phase or workset APIs.

## Add a boundary law

A boundary law registers its supported phase capabilities, required trace
fields, parameter schema, residual contributions, and tangent. Configuration
validation must reject missing assignments and incompatible phase/law pairs
before DoFs are distributed.

Contact-angle or contact-line laws are not ordinary boundary laws. They also
need a junction entity and must follow the junction extension below.

## Add a pairwise interface law

A new material-interface law consumes canonical traces and returns an oriented
`InterfaceExchange`. It must declare:

- participating species and phase-qualified mappings;
- local algebraic unknowns and their condensation tangent;
- conserved and constrained exchange components;
- entropy/admissibility conditions;
- geometry data and quadrature accuracy requirements;
- failure and globalization behavior;
- exact cancellation and orientation tests.

It must solve the local closure once. Independent left- and right-side flux
computations are nonconforming.

## Add layout-changing topology transitions

Breakup, coalescence, component birth/death, and phase-adjacency changes are
terminal in the first implementation. Supporting them later requires a
dedicated accepted-state transaction, not another retry of the pre-event step.
That extension must define:

- event detection and localization to a representable accepted time boundary;
- old-to-new `RegionId` lineage, including split and merge maps;
- conservative transfer of phase fields, transported histories, interface
  data, and any transformation state;
- mass/energy-consistent split or merge rules for regional scalar histories
  such as `p₀,r`;
- reconstruction of the global schema, block layout, gauges, nullspaces,
  multigrid transfers, and coarse spaces in a new `SpaceEpoch`; and
- a swept-volume/GCL and composite conservation audit before collective commit.

Until all of these capabilities are registered for an event class, detection
preserves the last accepted state and returns the terminal layout-transition
status.

## Add junctions or contact lines

Triple junctions and contact lines extend the phase graph with a
lower-dimensional hyperedge and add a distinct `JunctionWorkset` and
`JunctionOperator`. The extension must define:

- orientation and incidence among phases, interfaces, and exterior boundaries;
- force, energy, species, and contact-angle laws;
- junction motion and topology events;
- lower-dimensional quadrature and ownership rules;
- coupling to pairwise interface exchanges without double counting.

Pairwise interface code reserves identifiers and detection hooks for this
extension but does not implement placeholder junction physics.

## Add DG or cellwise hp

The first discretization is CG with uniform polynomial degree. DG adds
ordinary interior numerical fluxes, macroelement/subcell limiting, and
different conservation and activation policies. Cellwise `hp` additionally
changes workset keys, vectorization categories, transfer, multigrid, and
`FE_Nothing` conformity closure.

Both must reuse the phase, model, trace, exchange, and exact-operator
contracts. They may specialize evaluator adapters and stabilization kernels;
they may not fork the physical closures.

## Add an execution backend

An execution backend implements the evaluator and distributed-vector concepts
for every required workset kind. It owns traversal, batching, scratch,
communication, and scatter. It must reproduce the exact assembled reference
on the backend's supported scalar types.

A GPU backend must additionally define device-safe model storage, allocation
rules, kernel launch grouping for irregular cuts, host/device geometry-cache
lifetimes, and a multigrid/preconditioner strategy compatible with adaptive
meshes. CPU-only assumptions may not leak into physics kernels even though
MPI plus CPU SIMD is the initial target.

## Add a linear-algebra backend

PETSc, Trilinos, or another package may be added through the distributed
vector, sparse operator, Krylov, and preconditioner adapters described in
[linear algebra and mixed precision](18-linear-algebra.md). An adapter must state
supported scalar types and conversion behavior. It may not become the owner of
phase semantics or workset construction.

## Extension acceptance checklist

Every extension must demonstrate:

1. Registry construction and invalid-configuration rejection.
2. Exact residual/Jacobian agreement where it contributes.
3. Conservation and orientation checks where applicable.
4. Admissibility and deterministic failure handling.
5. Correct spatial, temporal, state, and prepared-data identity dependencies
   and cache invalidation.
6. State and regional-scalar transfer across refinement, repartition, and
   support changes.
7. MPI determinism within declared floating-point tolerances.
8. Documentation in the relevant contract page and the
   [decision ledger](24-decision-status.md).
