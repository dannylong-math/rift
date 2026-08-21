---
title: Architecture decision status
description: Living ledger of accepted, deferred, excluded, and open Rift architecture decisions.
---

# Architecture decision status

This page distinguishes committed contracts from extension points and open
implementation choices. A proposed mechanism must not be presented elsewhere
as an implemented guarantee.

## Accepted

### Phase and interface model

- A named phase selects one physics family for an entire run while its physical
  cells and algebraic support may change.
- The runtime domain model is a general phase graph with pairwise interface
  edges initially.
- Triple junctions have a reserved `JunctionOperator` extension rather than a
  pairwise special case.
- Material interfaces remain separated from exterior boundaries initially;
  contact-line creation is a terminal unsupported topology event.
- Interface closure uses canonical physical traces and one oriented exchange
  scattered to both phases.
- Interface temperature, transfer rates, heat fluxes, and normal velocity are
  local algebraic variables and are statically condensed.
- Swept phase activation uses the existing temporal mass/interface balance and
  a phase-specific policy. Fluid-solid transformation initializes declared
  stress-free/transformation history without a second conserved source.
- A global immutable chemical catalog maps phase-local compact species indices
  to stable species identities.

### Discrete spaces and geometry

- Continuous Galerkin is the first discretization.
- A phase declares multiple phase-local field-group DoFHandlers where element
  family or degree differs; `SpaceRegistry` owns the resulting spaces.
- Each phase uses ordinary elements on a solve-time support envelope and
  non-dominating `FE_Nothing` outside it.
- Polynomial degree is uniform initially; adaptive background-mesh
  `h`-refinement is required from the first implementation.
- A `LevelSetFieldSet` lives on the full background mesh. A registered encoding
  maps its component set to runtime phase and interface identities.
- Level-set transport is a globally coupled equation block and consumes the
  same closure-computed `VΓ` used by phase relative fluxes.
- A nonlinear attempt freezes the mesh, partition, active FE maps, DoF layout,
  candidate coupling graph, and sparsity.
- Trial geometry may move inside the fixed envelope using versioned geometry
  snapshots and safeguarded quasi-Newton linearization.
- Cushion width is displacement- and stencil-derived, with a nominal minimum
  of two or three cell layers and additional hanging-face conformity closure.
- A topology-preserving geometry regrouping with bijective `RegionId` lineage,
  unchanged layout, and a swept-GCL audit restarts nonlinear history in the
  same space. True breakup, coalescence, component birth/death, or adjacency
  change is terminal initially. An exhausted cushion abandons the
  open time step, rebuilds from the accepted checkpoint, and retries the
  complete step.

### State, models, and evaluation

- A central `StateStore` owns accepted, stage, trial, update, and rollback
  vectors; phases receive views.
- The initial vector implementation is deal.II's distributed block vector,
  without PETSc or Trilinos.
- Primary state, evaluation, and preconditioner scalar roles are separate.
- Model kernels are generic over scalar, SIMD, AD, and mixed-precision types.
- Runtime configuration selects from a compiled model registry; species and
  reaction-mechanism data are normally runtime values.
- deal.II iterators and evaluators are confined to execution backends.
- MPI plus CPU SIMD is the first performance target.
- Stage residuals use explicit swept-domain data and must satisfy the discrete
  geometric conservation law.
- Curved-background operators use one backend metric policy satisfying the
  declared static discrete metric identities and constant-state test.
- Transport-capable CG phases provide a conservative invariant-domain
  low-order graph and EOS-aware pairwise convex limiter. Entropy viscosity
  alone is not an admissibility guarantee, and no after-stage clipping is
  allowed.
- The exact operator has matrix-free residual and Jacobian actions plus an
  exact assembled reference implementation. The initial assembled Jacobian is
  a bounded-size, one-rank correctness facility.
- Approximate assembled or mixed-precision operators belong only to
  preconditioning.
- The initial scalable preconditioner composes global `p → 1` coarsening and
  deal.II-native matrix-free adaptive `h`-multigrid, physics-local solves,
  explicit nullspace handling, and cut-patch corrections; flexible
  outer-Krylov semantics are required.

### Lifecycle

- Mesh adaptation occurs through an atomic, rollback-capable transaction at an
  accepted time-step state.
- Mandatory CG transfer conserves phase totals globally; macroelement-local
  conservation is added where feasible.
- Candidate-interface cells may refine but do not initially coarsen.
- Transfer or adaptation audit failure rolls back rather than silently falling
  back to a lower-order remap.
- Level-set reinitialization is a trigger-plus-algorithm interface. It runs only
  on accepted states and must pass zero-contour and phase-volume audits. The
  default trigger is a signed-distance quality threshold; `EveryNSteps` remains
  the simplest deterministic trigger and fallback test policy.
- Stiffly accurate IMEX-ARK is the production time-integration baseline;
  Radau IIA is the verification integrator.
- Sourcey Markdown is the target narrative-documentation format.

## Deferred extensions

- DG and mixed CG–DG execution.
- Cellwise `p` and full `hp` adaptivity.
- GPU/device execution.
- PETSc and Trilinos adapters.
- Dynamic surface species, adsorption, surface diffusion, surface heat
  capacity, and surface PDEs.
- Triple junctions, contact lines, and contact-angle laws.
- Layout-changing topology transitions and their event-time regional remap.
- Solid–solid unilateral or frictional contact.
- Heterogeneous precision among primary state blocks.
- Fully consistent shape-Newton derivatives of cut geometry.

## Deliberately excluded from the initial contract

- Changing active FE indices or redistributing DoFs inside nonlinear residual
  evaluation.
- Raw deal.II iterator ranges as the primary physics API.
- Independent time advancement by phase systems.
- Independent computation of the two sides of a material-interface flux.
- Per-quadrature virtual model dispatch or shared mutable scratch.
- Automatic exterior contact-line handling.
- Automatic first-order transfer after an adaptation failure.
- Treating `FE_Nothing` as a physical zero state.

## Open implementation selections

The architecture permits these choices without changing its component
boundaries, but they must be settled before the associated feature is complete:

- the first concrete reinitialization algorithm;
- the first multiphase level-set encoding policy beyond the one-component
  two-region case;
- numerical tolerances for cushion containment, geometry transfer, phase
  conservation, surface energy, and reinitialization audits;
- refinement indicators and cross-phase priority rules;
- the first concrete invariant-domain graph and convex-limiter realization for
  each phase physics family;
- the initial Krylov methods, multigrid smoothers, coarse solvers, and
  mixed-precision schedules;
- the first end-to-end physical phase pairing and benchmark ladder;
- the exact Sourcey/Doxygen migration procedure for the existing API
  documentation workflow.

The unequal-`p₀` merge policy belongs to the deferred layout-changing topology
transition and is not an open choice for the initial implementation.

Changes to accepted items require an explicit architecture decision and an
update to every affected contract page and conformance test.
