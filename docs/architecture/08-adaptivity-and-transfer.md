---
title: Adaptivity and transfer
description: Atomic h-adaptation, active-envelope closure, state transfer, and rebuild contracts on p4est meshes.
---

# Adaptivity and transfer

Adaptive mesh refinement is part of Rift's first implementation. The initial
scope is adaptive `h` refinement on deal.II's p4est-backed distributed
triangulation with one uniform polynomial degree per configured space. A single
background mesh is shared by the full-background level-set field set and all phase-local
`DoFHandler`s. A rebuild changes this coupled system atomically or not at all.

Adaptation runs only from an accepted time level. The first implementation
constructs a complete shadow mesh, space, state, and operator bundle; the live
accepted bundle is never mutated during proposal or validation. No outside
handle can observe unpublished intermediate objects. An in-place adaptation
path is deferred until it has a concrete collective save/restore protocol and
fault-injection evidence equivalent to the shadow transaction.

## Active envelopes and hanging-node closure

For each phase, classification first marks its physical cells, then expands by
the declared fictitious-band and operator-stencil radius. The resulting active
mask is shared by every `DoFHandler` in that phase in the first implementation.
A future independently supported envelope must introduce its own envelope-group
identity and run the same closure and coarsening rules. Active cells select the phase finite
element; inactive cells select non-dominating `FE_Nothing`. A dominating
`FE_Nothing` would constrain an adjacent ordinary element to zero and is not
the intended semantics.

deal.II does not support the hanging-node case in which one coarse face meets a
set of refined children that mix an ordinary finite element and `FE_Nothing`.
Rift therefore closes each phase mask before distributing DoFs:

1. Start with physical, band, and all stabilization/reconstruction stencil
   cells active.
2. Inspect every coarse–fine face. If its fine children mix active and
   inactive states for a phase, activate every fine child touching that coarse
   face.
3. Publish owner masks to ghost copies, inspect locally relevant faces, and
   send owner-directed activation requests for any ghost cell that must become
   active. Owners combine requests with logical OR, reduce a global `changed`
   flag, republish the new masks, and repeat to a fixed point.
4. Apply the final mask consistently to all spaces belonging to that phase.

Closure is monotone: it may activate cells but never deactivate them.
`GridTools::exchange_cell_data_to_ghosts` may refresh ghost copies, but a
separate sparse owner-request exchange is required because ghost writes do not
activate the owning rank's cell. The transaction records cells added by
closure so memory estimates and diagnostics do not confuse them with the
requested physical band.

Refined children inherit their parent's phase-mask vector. A sibling family may
coarsen only if all children have identical mask vectors for every phase and no
child is protected by a candidate interface, band, or stencil rule. Otherwise its
coarsening flags are cleared. Refinements added by p4est balance inherit the
parent mask and are safe. Only locally owned active cells are marked; deal.II's
collective `prepare_coarsening_and_refinement()` is allowed to alter flags to
enforce mesh constraints. Automatic repartitioning follows execution.

## Transaction API

Mesh adaptation and envelope-only support-allocation changes use the same staged
protocol, although the former changes the triangulation:

```cpp
struct RebuildCandidate {
  SpaceEpoch provisional_space_epoch;
  MeshSnapshot mesh;
  SpaceSnapshot spaces;
  StateStore states;
  GeometrySnapshot geometry;
  OperatorBundle operators;
  TransferAudit audit;
};

RebuildResult propose_adaptation(const AcceptedSnapshot &live,
                                 const AdaptationRequest &request);

CommitResult validate_and_commit(RebuildCandidate &&candidate,
                                 AcceptedSnapshot &live);
```

For an envelope-only change, Rift builds shadow `DoFHandler`s and active FE
maps on the unchanged mesh, distributes shadow DoFs and constraints, transfers
persistent fields through the same `SpaceDraft`/`TransferWorkspace` protocol,
rebuilds geometry and the regional schema, finalizes the target `StateStore`,
fills newly active fictitious unknowns, and only then builds backend bindings,
worksets, and preconditioners.
Before any target geometry, workset, vector, or backend object is built, the
transaction reserves a unique, never-reused provisional `SpaceEpoch` and stamps
the complete shadow bundle with it. Only the final snapshot swap makes that
epoch live. Rejection burns the provisional identity and leaves the live epoch
unchanged, preventing a later candidate from aliasing stale target caches.

For an `h`-adaptation transaction, the manager initializes a shadow
triangulation from the accepted coarse mesh and p4est forest, recreates the old
spaces and source state there, and performs these collective steps only on
that transaction-owned bundle:

1. Compute refinement and coarsening marks from ghost-synchronized indicators,
   enforce protected-band and sibling rules, and close proposed phase masks.
2. Call the triangulation's collective preparation hook, incorporate any flags
   added for balance, and repeat mask/flag closure until both are stable. Set or
   transfer each phase space's future active FE indices from that final mask.
3. Classify every state entry as persistent, derived, or discardable. Create
   exactly one deal.II `SolutionTransfer` object per `DoFHandler`/`SpaceId`.
   Pass all persistent vectors on that space to one ordered preparation call,
   using locally relevant, ghost-updated sources. Prepare phase masks and other
   cell-attached metadata in the same declared attachment order.
4. Execute refinement/coarsening and automatic repartition exactly once. Keep
   the corresponding `DoFHandler` objects alive, verify their transferred
   active FE indices against the final masks, redistribute DoFs in stable
   order, build constraints, form the field-only `SpaceDraft` and maximum
   limiter graphs, and allocate owned field buffers in `TransferWorkspace`.
5. Interpolate each prepared vector list exactly once in preparation order and
   distribute constraints in the workspace. This produces provisional target
   field data, not a partially constructed `StateStore`, and does not yet claim
   conservative physical transfer.
6. Reconstruct and audit target geometry from the transferred full-background
   level-set buffer and the draft space view. Old and target cut quadrature are
   now both available.
   For a topology-preserving mesh change, require a deterministic bijection
   from every old `RegionId` to its target identity.
7. Build the target `RegionalConstraintSchema`, finalize the target
   `StateLayout` and immutable `SpaceSnapshot` (which owns no backend binding),
   construct the target `StateStore` by
   adopting the field buffers, allocate its global blocks, and transfer each
   non-DoF value and required history through that region
   bijection. Copy solved `p₀,r` values and histories semantically, rebuild
   gauges/nullspaces, and audit that no regional row was lost or duplicated.
8. Run each physics-specific transfer policy using the two spatial geometries,
   initialize newly active band unknowns, and audit conservation and
   admissibility.
9. Build the separately owned exact backend binding from the finalized space,
   derive spatial worksets, initialize an empty temporal-workset cache/factory,
   build the chosen approximate preconditioner, and run all remaining
   structural and physics validators. Temporal bundles are created later from
   each stage's `TemporalGeometrySnapshot`.
10. Commit the entire candidate collectively by swapping the complete snapshot
   bundle. On any validation failure, discard the shadow candidate and leave
   the accepted bundle untouched.

[`SolutionTransfer`](https://dealii.org/developer/doxygen/deal.II/classSolutionTransfer.html)
provides the mesh-change transport, but nodal interpolation alone is not a
conservative multiphysics transfer. Preparation and interpolation order is part
of Rift's contract, as is providing valid locally relevant source data.

## State-transfer policy

Each equation set registers a `StateTransferPolicy`:

```cpp
struct StateTransferPolicy {
  void transfer_persistent(const TransferContext &,
                           ConstStateView old_state,
                           StateView new_state,
                           ConservationAudit &) const;
  void initialize_new_band(const ExtensionContext &, StateView) const;
  ValidationReport validate(const StateView &) const;
};
```

Persistent entries include the accepted solution and the time-history vectors
required by the selected integrator. Stage vectors are persistent only if an
explicit future policy permits mid-step adaptation. Residuals, Newton
corrections, Krylov vectors, ghost mirrors, constitutive caches, and
preconditioners are derived or discardable and are rebuilt instead of
transferred.

Regional unknowns and histories have no `DoFHandler` and therefore never pass
through deal.II `SolutionTransfer`. `RegionalConstraintSystem` supplies an
explicit `RegionalStateTransfer` keyed by the verified old-to-target
`RegionId` map. Topology-preserving refinement and repartition must keep a
bijection; a split, merge, birth, or death is the separately deferred
layout-transition contract in [extension points](23-extension-points.md).

The policy identifies conservative variables and corrects interpolation to
meet mandatory global conservation tolerances for mass, momentum, total
energy, and species. Macroelement-local conservation is enforced where the
physics-specific method can provide it. The policy also restores EOS
admissibility, positivity, and species-sum constraints without concealing an
unacceptable conservation defect.
Low-Mach or other equation sets define their own invariants. Newly activated
phase DoFs are initialized by the same extension model used by the fictitious
band, not by zero fill. Level-set interpolation is audited immediately; any
scheduled reinitialization remains a separate accepted-state transaction with
its own zero-contour and phase-volume audit.

If a correction cannot satisfy both conservation and admissibility, validation
fails and the transaction rolls back. The audit records pre- and post-transfer
integrals, correction magnitudes, minimum admissibility margins, and cells that
were newly activated.

## Identity and cache invalidation

`PhaseId`, `SpaceId`, `FieldId`, and `InterfaceId` are semantic and stable.
deal.II `CellId` is suitable for diagnostics and maps across repartition while
the same cell exists, but refinement and coarsening create different cells; all
persistent keys therefore also name a space epoch. Active-cell indices,
level/index pairs, local DoF numbers, and matrix-free batch numbers are never
persistent identifiers. Data that must survive a mesh change uses
`SolutionTransfer` or an explicit cell-data transfer, not assumptions about
those indices.

A mesh commit advances the space epoch and publishes matching new geometry and
state epochs; it invalidates all worksets, matrix-free data, sparsity,
linearizations, and preconditioners. A support-envelope-only commit also advances
the space, geometry, and state epochs even though mesh topology is unchanged.
The dependency rules in [Solver lifecycle](22-solver-lifecycle.md) then rebuild or
retain caches mechanically. [Execution backend](17-execution-backend.md) defines
the new matrix-free binding contract.

## Failure policy and tests

Every rank participates in prepare, execute, validate, and commit. A local
failure becomes a collective rejection before a new snapshot is published.
After the transaction-owned triangulation has been modified, rollback means
discarding that complete shadow bundle; Rift never tries to invert p4est
operations cell by cell. Resource estimates may reject a proposal before the
shadow mesh is mutated. An MPI communicator failure is fatal rather than a
normally recoverable validation result, but the accepted bundle is never
partially published.

Tests cover refinement, coarsening, repartition, envelope growth and shrinkage,
multiple phase `DoFHandler`s, closure propagation across MPI boundaries, and
the forbidden mixed `FE_Nothing` hanging-face pattern. Transfer tests measure
each conserved integral and admissibility margin, exercise newly active band
cells, and compare serial with repartitioned results. Fault injection after
every transaction stage must prove the old residual remains evaluable.
Successful rebuild tests compare assembled and matrix-free residuals and `Jv`
on the new snapshot, then verify that every old handle is rejected as stale.
Rejected-shadow tests also prove that a later proposal receives a different
provisional space identity.
Closed low-Mach cases additionally verify semantic transfer of `p₀,r` and
its time history, plus reconstructed gauges and nullspaces, across refinement
and repartition.

See deal.II's
[`parallel::distributed::Triangulation`](https://dealii.org/developer/doxygen/deal.II/classparallel_1_1distributed_1_1Triangulation.html)
and [`FE_Nothing`](https://www.dealii.org/current/doxygen/deal.II/classFE__Nothing.html)
documentation for the underlying mesh and finite-element mechanisms.
