---
title: Linear algebra and mixed precision
description: Central state storage, block metadata, distributed vectors, and scalar roles for Rift's coupled solves.
---

# Linear algebra and mixed precision

Rift stores coupled algebraic state centrally. Phase systems, the geometry
system, time integrators, nonlinear methods, and preconditioners receive typed
views rather than owning independent solution vectors. This makes rollback,
block norms, monolithic Krylov methods, and cross-phase preconditioning
explicit and consistent.

The initial implementation uses deal.II distributed vectors without PETSc or
Trilinos. The public Rift contracts describe vector operations rather than a
specific concrete type so an external backend can be added later.

## Layout and values

`StateLayout` describes the blocks belonging to one `SpaceEpoch`. It contains
stable semantic identifiers, ownership and relevance index sets, vector
partitioners, field components, scaling information, and links to the
responsible phase or geometry object.

`StateStore<StateNumber>` owns vector instances using that layout:

```cpp
template <class StateNumber>
class StateStore
{
public:
  StateVector<StateNumber> &accepted();
  StateVector<StateNumber> &checkpoint();
  StateVector<StateNumber> &stage(StageIndex);
  StateVector<StateNumber> &trial();
  StateVector<StateNumber> &update();
  ScratchLease<StateNumber> lease(OperatorScratchRole);

  PhaseStateView<StateNumber> phase(PhaseId, StateSlot);
  GeometryStateView<StateNumber> geometry(StateSlot);
};
```

The first `StateVector` realization wraps
`deal.II::LinearAlgebra::distributed::BlockVector<StateNumber>`. Field groups
with different DoFHandlers occupy different blocks. Small regional unknowns,
such as low-Mach thermodynamic pressures, receive explicit distributed or
owner-rank blocks and are never hidden inside phase objects.

Block identifiers are semantic values such as `(PhaseId, FieldGroupId)` and
`GeometryFieldId`; they are not raw vector positions. The layout maps them to
positions for the current space epoch.

## Scalar roles

Rift distinguishes three scalar roles:

| Role | Purpose |
| --- | --- |
| `StateNumber` | Persistent nonlinear, stage, and Krylov-vector storage |
| `EvaluationScalar` | Scalar, SIMD, or automatic-differentiation value used inside a kernel |
| `PreconditionerNumber` | Independently selected storage and compute precision for auxiliary operators |

All primary blocks use one `StateNumber` in the first implementation. This
keeps dot products, norms, ghost exchange, and Krylov recurrences well defined.
Mixed precision begins with lower-precision preconditioner data and inner
solves, plus controlled conversion at the preconditioner boundary.

`EvaluationScalar` may be `float`, `double`, a CPU SIMD array, an AD wrapper,
or a supported composition of those types. Model policies must use scalar
traits for constants and mathematical operations and must not assume IEEE
double storage.

## Vector concept

The coupled solver depends on a small `DistributedVector` concept:

```cpp
template <class V>
concept DistributedVector = requires(V x, const V y) {
  typename V::value_type;
  x = typename V::value_type{};
  x.add(typename V::value_type{}, y);
  x.sadd(typename V::value_type{}, typename V::value_type{}, y);
  x.update_ghost_values();
  x.zero_out_ghost_values();
  x.compress(VectorOperation::add);
  { x.l2_norm() };
};
```

The production concept will also cover locally owned elements, partitioner
identity, local access needed by matrix-free evaluation, and collective
compatibility checks. Algorithms must not downcast the concept to a deal.II
vector outside the initial adapter.

## Ownership and mutation

The `StateStore` is the sole owner of primary vectors. A `PhaseStateView` is a
non-owning collection of typed block views and field component maps. It cannot
resize or replace its blocks. Resizing occurs only when a new `StateLayout` is
committed by the [solver lifecycle](22-solver-lifecycle.md).

During a space rebuild, a transaction-local `TransferWorkspace` may own
field-only interpolation buffers against a `FieldLayoutDraft`. These are not
primary state and cannot escape the transaction. After regional rows finalize
the complete `StateLayout`, the new `StateStore` adopts or copies those buffers
under the atomic rebuild contract.

Const source vectors and mutable destination vectors are explicit in every
operator call. Ghost-state transitions are owned by the execution driver, not
by local physics kernels. Concurrent worksets use thread-local scratch and may
only scatter through backend-approved accumulation paths.

Rollback swaps or restores complete state slots. It never asks each phase to
reconstruct an independently owned partial checkpoint.

Low-order, high-order, limited-candidate, and other operator-temporary vectors
are tagged scratch leases rather than persistent solution slots. Their lease
records the evaluation identity, prevents incompatible aliasing, and is
released when the operator call or rejected candidate ends.

## Block norms and convergence

Raw Euclidean norms are not meaningful across density, momentum, energy,
pressure, level set, and solid-history blocks. `StateLayout` therefore stores
field-specific absolute and relative scales. Nonlinear convergence, line
search, temporal error control, and inexact-Newton forcing use named block
norms plus mandatory physical diagnostics.

Convergence of a global norm cannot override a failed admissibility,
conservation, interface-closure, cushion-containment, or GCL check.

## Preconditioner boundary

A preconditioner receives the exact residual vector in `StateNumber`, converts
only the required blocks into `PreconditionerNumber`, applies its approximate
services, and converts the correction back. It reports inner failures,
iteration counts, and conversion costs.

Low precision is never used to evaluate the accepted exact residual merely
because the preconditioner uses it. Flexible Krylov methods are required when
inner tolerances or precision policies vary during a solve.

## Required tests

1. Block-view operations reproduce operations on the complete vector.
2. Layout mismatches and stale `SpaceEpoch` views fail deterministically.
3. Ghost updates and additive compression are correct under MPI.
4. Rollback restores every phase, geometry, scalar, and history block.
5. Block-scaled norms are invariant under MPI partitioning.
6. Float and double auxiliary preconditioners converge to the same exact
   residual tolerance where conditioning permits.
7. SIMD and scalar kernels produce equivalent residuals.
8. AD-seeded Jacobian actions agree with exact assembled actions and finite
   differences.

GPU vectors, heterogeneous primary-block precision, and PETSc or Trilinos
adapters are later [extension points](23-extension-points.md).
