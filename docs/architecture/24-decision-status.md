---
title: Architecture decision status
description: Living ledger of accepted, deferred, excluded, and open Rift architecture decisions.
---

# Architecture decision status

This page distinguishes committed contracts from extension points and open
implementation choices. A proposed mechanism must not be presented elsewhere
as an implemented guarantee.

## Conformance state

The contracts below are architecture targets, not claims that every target is
already implemented. The phase-graph and discrete-state baselines currently
have serial 2D/3D evidence, but their approved refinement remains incomplete
until run provenance, deterministic callback behavior, UTF-8 validation,
distributed adaptive support masks, collective state semantics, and the
required MPI tests and coverage gates have landed. Later solver components
remain unimplemented unless their own pages explicitly say otherwise.

## Accepted

### Phase and interface model

- A named phase selects one physics family for an entire run while its physical
  cells and algebraic support may change.
- Every graph belongs to an explicit, communicator-consistent
  `RunConfigurationId` and receives a never-reused, communicator-consistent
  `PhaseGraphInstanceId`. Graph-local phase and interface indices are combined
  with that provenance at component boundaries so identities from different
  runs or graph constructions cannot be mixed accidentally.
- `RunConfiguration::create(MPI_Comm)` supports live intracommunicators wholly
  derived from the current `MPI_COMM_WORLD`: world, self, duplicates, splits
  and subgroups, and Cartesian or otherwise reordered communicators.
  Intercommunicators and intracommunicators containing dynamic or MPI Sessions
  processes from an unrelated MPI world are expected unsupported inputs.
- `RunConfigurationId` has `{origin_world_rank, per_origin_sequence}` semantics.
  Rank zero of the supplied communicator is translated to its world rank and
  allocates the sequence, making identifiers collision-free and never reused
  during one MPI execution even for disjoint supported communicators. There is
  no uniqueness promise across executions or restarts.
- Actual MPI operation failures are fatal because collective recovery cannot be
  guaranteed. Successfully agreed logical unsupported conditions and finite
  identifier exhaustion may return structured expected errors.
- `PhaseGraph` may be constructed, copied, or moved while retaining its
  provenance, but assignment is deleted so borrowed descriptors cannot be
  invalidated by in-place graph replacement.
- The runtime domain model is a general phase graph with pairwise interface
  edges initially.
- The initial graph has at most one edge for each unordered pair of named
  phases. Its `InterfaceOperatorKey` selects the complete composite interface
  law for that pair, not one independently ordered physical contribution.
- Interface compatibility is a deterministic, side-effect-free construction
  query invoked once per structurally valid unique edge in canonical
  interface-name order. A returned reason is configuration incompatibility;
  an exception is a registry/program failure and propagates without producing
  a graph. Supplying any interface specification requires the query even when
  structural errors prevent all edges from resolving.
- Names, incident-phase references, and registry keys are validated as RFC
  3629 UTF-8. Canonical ordering uses exact UTF-8 bytes without normalization,
  case folding, or locale dependence.
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
- The first implementation uses one support mask per phase, shared by all of
  that phase's field-group `DoFHandler`s. Geometry and stencil policies provide
  requested cells; `SpaceRegistry` applies monotone hanging-face conformity
  closure and records cells added by closure. Independently supported fields
  require a future envelope-group identity.
- Replicated field schema is validated collectively and separately from
  owner-partitioned support masks. Each rank supplies locally owned active
  cells; their union is the global envelope. Owners publish masks to ghosts,
  receive activation requests, and iterate closure to global convergence.
- Active FE indices are assigned only on locally owned cells. deal.II DoF
  distribution communicates them to ghosts, after which locally owned and
  ghost selections are verified; artificial cells are not queried.
- Polynomial degree is uniform initially; adaptive background-mesh
  `h`-refinement is required from the first implementation.
- Space construction receives an immutable mesh snapshot with a never-reused
  `MeshSnapshotId` rather than an externally mutable triangulation. The mesh
  communicator is authoritative and its lifetime encloses every attached
  DoFHandler, layout, and state vector. Run and mesh communicators may be
  identical or `MPI_CONGRUENT`, but not reordered `MPI_SIMILAR` or unrelated
  communicators. All ranks in one construction call must supply triangulations
  built collectively on the same mesh-communicator context; MPI does not expose
  a portable context identity with which Rift could diagnose ranks alternating
  distinct but individually congruent communicators.
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
- The initial state is a distributed vector bundle satisfying Rift's semantic
  block-vector contract. Independently numbered fields may be backed by
  separate native deal.II distributed vectors; no PETSc or Trilinos adapter is
  required initially.
- Run, graph, mesh, space, `StateStoreId`, state-snapshot, accepted-state, and
  level-set revision identities are communicator-consistent and never reused
  within their provenance. Process-global atomic counters are not distributed
  identity authorities.
- State construction, sealing, publication, discard, and regional-value
  synchronization are collective. Snapshot reads and locally owned field
  edits are local; immutable snapshots may be shared for reading while store
  mutation is externally serialized.
- Store construction agrees layout/policy first on the retained run
  communicator and enters the mesh communicator only after exact agreement.
  Each later transition starts with an operation/store/transaction-or-snapshot
  descriptor; rejected logical transitions consume no state identity.
- Transactions retain a communicator/store/transaction tombstone after move,
  abandonment, sealing, or store expiry so asymmetric invalid calls can return
  one fixed-size allocation-free collective error. Local field access uses a
  full space/phase/group `StateFieldReference`, not a bare numeric group ID.
- Level-set revision identity compares exact `double` object representation:
  `+0.0` and `-0.0` are different, the same NaN payload is unchanged, and a
  different NaN payload is a change. Finiteness is a separate geometry/model
  validity question.
- Mutable transactions hold weak store authority and fail safely if the store
  has expired. A sealed candidate records its accepted-root lineage and cannot
  replace a newer accepted sibling. Intentional rollback is a separate
  lifecycle operation.
- Store lookup retains accepted, previous, and explicitly retained private or
  pinned snapshots. Older external immutable snapshot handles remain valid,
  but publication does not retain every historic vector bundle indefinitely.
- Bounded retention uses one unpinned transient private slot plus at most the
  configured number of pinned private records. Seal replaces the transient;
  pin converts it to pinned; unpin promotes it to transient and evicts the
  former transient. Active transaction bases retain data without protecting
  lookup IDs or consuming pin capacity.
- Regional scalars have one authoritative owner on mesh-communicator rank zero
  while immutable snapshots expose a synchronized exact-bit value on every
  rank. Collective writes agree entry and complete binary64 representation,
  update the root-owned backend plus a replicated cache, and reserve no state
  identity; raw owner-only vector indexing is an internal backend concern.
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
