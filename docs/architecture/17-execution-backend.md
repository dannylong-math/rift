---
title: Execution backend
description: MPI, SIMD, vector, and traversal contracts for assembled and matrix-free operator execution.
---

# Execution backend

The execution backend owns the mechanics that physics kernels must not know:
DoF lookup, ghost exchange, finite-element evaluation, local accumulation,
thread-safe scatter, MPI compression, and SIMD batching. Rift initially targets
distributed-memory MPI with CPU SIMD. GPU execution is not part of the first
contract, although evaluator-neutral kernels avoid making it impossible later.

There are two exact backends. The production backend applies residuals and
Jacobian actions without assembling the global Jacobian. The reference backend
assembles the same residual derivative for tests, diagnostics, and small
verification problems. They call the same kernels and consume the same cut
quadrature records. Preconditioning is a third, explicitly approximate path;
it is not an alternate interpretation of the exact operator.

## Linear algebra boundary

Public Rift algorithms depend on a narrow distributed block-vector concept,
not directly on a vendor solver package:

```cpp
template<class V>
concept DistributedBlockVector = requires(V v, const V cv,
                                          BlockId b, GlobalIndex i) {
  typename V::value_type;
  { v.owned_partition() };
  { v.relevant_partition() };
  { cv.read(b, i) };
  { v.add(b, i, typename V::value_type{}) };
  { v.update_ghost_values() };
  { v.compress_add() };
  { v.zero_out_ghost_values() };
};
```

The initial model is implemented with deal.II
`LinearAlgebra::distributed::BlockVector<StateNumber>`. PETSc and Trilinos are not
initial dependencies. A `StateLayout` maps stable `SpaceId` and `FieldId`
values to blocks and records locally owned and locally relevant index sets.
Only the central `StateStore` creates persistent vectors; evaluators receive
borrowed, epoch-checked views.

Ghost state is explicit. A source vector is made read-ready before traversal;
an output vector begins with owned entries initialized and ghost entries in a
known writable state; local contributions are accumulated and then compressed
with addition. No kernel may trigger communication. Aliasing between source,
direction, residual, and image is rejected unless an operation explicitly
documents it.

## Space binding and evaluator creation

A finalized `SpaceSnapshot` may contain several phase-local or field-group
`DoFHandler`s. Each uses an active finite element inside its fixed envelope and
`FE_Nothing` outside it. The backend binds those spaces to one or more deal.II
evaluation contexts and publishes a stable handle only after the complete
`StateLayout` and maximum limiter graphs exist:

```cpp
struct BackendBinding {
  BackendBindingId id;
  SpaceEpoch space_epoch;
  StateLayoutView layout;
  SpaceEvaluatorTable evaluators;
  ConstraintLookupTable constraint_lookups;
};

BackendBindingHandle build_binding(const SpaceSnapshot &,
                                   const BackendConfiguration &);
```

The backend owns the `BackendBinding` resource. The top-level snapshot bundle
pairs its handle with the finalized space; `SpaceSnapshot` does not contain the
handle and backend construction never calls back into space finalization.

An implementation may place compatible spaces in one
[`MatrixFree`](https://dealii.org/developer/doxygen/deal.II/group__matrixfree.html)
object or use several grouped objects. That choice is private to the backend;
physics code addresses spaces by `SpaceId`, never by a `MatrixFree` DoF-handler
number. `StateLayoutView` borrows the authoritative vector partitioners from
the finalized space. Reinitialization after a space change rebuilds backend
evaluator/MatrixFree communication partitioners,
constraint lookup/evaluator tables, borrowed-mapping evaluator state, SIMD
batches, and face information as one new binding.
The background mapping and `MatrixFree` batch topology are immutable within a
`SpaceEpoch`.

Regular cell and mesh-face worksets use deal.II `FEEvaluation` and
`FEFaceEvaluation` on those stable batches; a geometry snapshot supplies
batch/lane masks rather than repacking or reinitializing `MatrixFree`.
Cut-volume and embedded-interface worksets use a separate compact path and
evaluate at geometry-provided arbitrary points, for example through
[`FEPointEvaluation`](https://www.dealii.org/current/doxygen/deal.II/classFEPointEvaluation.html).
That point path may group compatible records for each geometry snapshot and
pad partial SIMD batches with an inactive-lane mask. Kernels must produce the
same answer for an active lane whether it is evaluated alone or in a full
batch.

The structural workset bundle is backend-neutral. Each exact backend owns a
derived traversal schedule keyed by `(WorksetBundleId, BackendBindingId)` and,
for temporal work, `TemporalWorksetBundleId`. A schedule may contain private
batch numbers, evaluator indices, coloring, or scratch offsets; none of those
identities is written back into the shared workset.

## Static mapping and metric identities

`MeshStore` owns the background mapping. The execution backend borrows that
mapping through an immutable `SpaceSnapshot` and owns the corresponding
`MetricEvaluationPolicy` and evaluator bindings. On curved cells, conservative and split-form
operators must use discrete contravariant metrics, Jacobians, normals, and
quadrature that satisfy the declared static metric identities. Geometry-field
motion changes the embedded cut, not these background mapping metrics within a
`SpaceEpoch`.

Both exact backends consume one metric record per evaluator context. A kernel
must not recompute an alternative normal or Jacobian from coordinates. The
metric policy declares its polynomial/quadrature requirements and fails space
construction when the requested mapping/operator combination cannot preserve
constant states. Tests include constant compressible states on curved mapped
meshes, discrete divergence of metric terms, scalar/SIMD agreement, and
assembled/matrix-free equality. This static mapped-mesh invariant is separate
from the swept-interface GCL.

## Backend-neutral invocation

Both exact backends implement the same high-level operation:

```cpp
template<class Evaluator, class Kernel>
void evaluate_workset(const Workset &workset,
                      Evaluator &evaluator,
                      const Kernel &kernel,
                      ConstStateView source,
                      StateView destination);
```

The evaluator gathers DoFs, supplies point views, integrates returned
contributions, applies the entity's canonical scatter rule, and releases all
temporary storage before returning. Thread-local scratch and local result
buffers belong to the evaluator pool. Their capacity may be cached by workset
shape, but their contents never cross calls.

The matrix-free residual loops over owned worksets, reads relevant ghost
values, scatters locally, and compresses once at a defined synchronization
boundary. The exact Jacobian action follows the same traversal with tangent
point views. The assembled residual may scatter into the same native
distributed block vector. The initial assembled Jacobian reference uses
FEValues-style local matrices and constraint-aware insertion into a one-rank
deal.II block sparse matrix on bounded verification problems. `MeshWorker` or
`WorkStream` may schedule assembled cell and face work, but its iterators stay
behind the evaluator boundary. See
[`MeshWorker`](https://www.dealii.org/current/doxygen/deal.II/namespaceMeshWorker.html)
and [Operator evaluation](16-operator-evaluation.md).

The assembled and matrix-free implementations are allowed to order floating
point operations differently. They are not allowed to choose different
quadrature, normals, stabilization stencils, boundary laws, or constitutive
states. Their numerical comparison therefore uses scale-aware tolerances rather
than bit equality.

## Approximate preconditioner path

`ApproximateOperatorBuilder` consumes a declared approximation specification,
such as block decoupling, frozen coefficients, reduced coupling, or a simplified
interface term. It returns an object satisfying a solver-facing apply concept.
Its caches are labeled with `SpaceEpoch`, `BackendBindingId`,
`WorksetBundleId`, optional `TemporalWorksetBundleId`, `GeometrySnapshotId`,
`StateSnapshotId`, optional `TemporalGeometrySnapshotId`, time stage,
model-data revision, the approximation specification hash, and any
`LimiterSnapshotId` whose factors it consumes. An approximate operator is never passed to
`apply_matrix_free_exact_jacobian`, and an exact-backend test never silently
substitutes it for `J`.

The initial implementation can use deal.II-native Krylov solvers and
preconditioners over Rift vector views. A future external linear-algebra adapter
must preserve block semantics, ownership, ghost transitions, and failure
reporting; the rest of the architecture does not assume PETSc or Trilinos.

## Invalidation and failure

A backend call first validates its snapshot tuple. A changed mesh or space
epoch makes every binding and vector layout stale. A changed geometry snapshot
invalidates cut/interface worksets and any geometry-dependent evaluator data.
A changed workset-bundle id invalidates every derived backend traversal
schedule even when its space and geometry identities are unchanged.
A changed state snapshot or time stage invalidates coefficient and
preconditioner caches but not a pure space binding. The precise dependency graph is defined in
[Solver lifecycle](22-solver-lifecycle.md).

Failures are typed. A stale handle, incompatible layout, missing ghost update,
unsupported evaluator combination, or non-finite local contribution is an
operator error with entity and epoch context. MPI ranks agree on failure before
leaving a collective phase. Backend construction occurs off to the side during
rebuild transactions, so a failed allocation or initialization leaves the live
binding usable.

## Verification obligations

Backend tests run one-cell, face-pair, cut-cell, hanging-node, and
MPI-partition cases. They compare scalar and SIMD lanes; one-rank and multi-rank
residuals; assembled and matrix-free residuals; and exact matrix-free `Jv`
against assembled `J*v` on one rank. Distributed `Jv` is also compared with
the corresponding one-rank reference result and with local element actions.
Tests vary block ordering and use multiple
`DoFHandler`s to expose accidental index assumptions. Ghost-state misuse and
every stale epoch, bundle, and binding combination must fail deterministically.
Two exact backends must derive independent schedules from one structural
bundle and reproduce the same active entities. A rebuild test
constructs a shadow binding, injects a failure, and verifies that the old
operator still evaluates unchanged. Adaptation-specific rebuild coverage is in
[Adaptivity and transfer](08-adaptivity-and-transfer.md).
