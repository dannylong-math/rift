---
title: Operator evaluation
description: Contracts that let one set of multiphysics kernels drive exact matrix-free and assembled operators.
---

# Operator evaluation

Rift separates the mathematical definition of an operator from traversal,
finite-element evaluation, and linear-algebra storage. A physics kernel sees
typed values at quadrature points; it does not see a deal.II cell iterator,
DoF index, global vector, or mutable scratch object. An execution backend turns
mesh entities into evaluator-neutral views, calls the kernel, and scatters its
response. This boundary is the main mechanism for using the same equations in
the production matrix-free operator and the assembled reference operator.

The first implementation supports volume terms, interior mesh faces, physical
boundary patches, embedded material-interface patches, and fictitious-band
stabilization faces. These are distinct entity kinds, not three variants of a
single iterator range: their quadrature domains, input sides, orientation, and
scatter rules differ. Contact-line entities are outside the initial scope.

## Kernel contract

An equation kernel is a small, statically selected object. Runtime choices,
such as which phase occupies a workset or which interface law applies, are
resolved before entering a quadrature loop. The scalar type remains a template
parameter so the same kernel can evaluate values, SIMD lanes, or directional
derivatives without virtual calls in the hot loop.

```cpp
template<class Scalar, int dim>
struct VolumePointView {
  StateView<Scalar> state;
  GradientView<Scalar, dim> gradient;
  GeometryPointView<Scalar, dim> geometry;
  TimePointView<Scalar> time;
};

template<class Scalar, int dim>
struct FluxContribution {
  StateView<Scalar> value;
  GradientView<Scalar, dim> gradient;
};

template<class Equation, class ConstitutiveModel>
struct VolumeKernel {
  template<class Scalar, int dim>
  FluxContribution<Scalar, dim>
  evaluate(const VolumePointView<Scalar, dim> &point) const;
};
```

The shown types are conceptual. Their concrete implementations may be
lightweight spans, fixed-size tensors, or expression views. They must not own
global state. Constitutive models such as equations of state, transport laws,
reaction models, and closures are template parameters or immutable members of
the kernel. Configuration data may be selected at runtime when constructing a
finite set of kernel specializations.

An interface kernel receives both traces and one geometric orientation. It
returns a single `InterfaceExchange` containing explicit minus-side and
plus-side contribution bundles plus one conservation ledger. The backend
scatters each bundle exactly once. Opposite signs apply to common conserved
transfer under the two outward normals; phase-qualified species fluxes may
differ according to interface-reaction stoichiometry. This makes discrete
balance a structural property rather than a convention duplicated by two
phase solvers. Boundary kernels similarly return a contribution after applying
a typed boundary law. Stabilization kernels consume two traces from the same
phase and are evaluated on the complete fictitious-band stencil.

## Worksets, not iterator ranges

The domain layer creates immutable worksets from a `SpaceSnapshot` and a
`GeometrySnapshot`. A workset is homogeneous enough that its inner loop has no
model or entity dispatch. A representative key contains:

```cpp
struct WorksetKey {
  WorksetBundleId bundle;
  EntityKind entity_kind;
  PhasePair phases;
  SpaceId trial_space;
  SpaceId test_space;
  QuadratureStrategy quadrature;
  LawId law;
  SpaceEpoch space_epoch;
  GeometrySnapshotId geometry_snapshot;
};
```

Additional structural keys may include finite-element numbers, mapping type,
and update flags. SIMD lane occupancy and evaluator/batch numbers are keys only
for a backend-private derived schedule. A structural workset record refers
to space-owned stable entity/local-index descriptors and a central geometry
quadrature record, never to a backend-owned batch or evaluator. Each backend
derives a private schedule keyed by `WorksetBundleId` and `BackendBindingId`.
The record never becomes a persistent user-facing iterator. Regular `MatrixFree` batch topology
remains fixed for a `SpaceEpoch`; geometry changes select stable batches and
lanes through masks. Separately packed cut and interface records may be
reordered for SIMD and cache locality while keeping domain classification in
one place.

`GeometryTopologyManager` is the sole authority that generates cut-cell and
embedded-interface quadrature for a geometry snapshot. It materializes the
points, weights, normals, curvature data, and orientation in backend-neutral
records. Both exact execution paths consume those same records; neither asks
an evaluator to regenerate a cut rule independently. A backend may use
[`FEPointEvaluation`](https://www.dealii.org/current/doxygen/deal.II/classFEPointEvaluation.html)
or another point evaluator at the stored points, but the geometric measure
being integrated must not change.

State-dependent prepared data is bound separately by an `EvaluationContextId`
covering the state snapshot, optional temporal-geometry snapshot, time stage,
and model/policy revisions. In particular, conservative-limiter factors are an
immutable derived `LimiterSnapshot` for that context and the structural
`LimiterWorkset`; they are not stored in the workset or inferred from a live
vector. See [stabilization and admissibility](12-stabilization-and-admissibility.md).

## Residual, Jacobian action, and reference matrix

Rift exposes three mathematically related operations and one deliberately
separate approximation:

`OperatorContext` and `LinearizationContext` pin `SpaceEpoch`, the matching
`BackendBindingId`, `WorksetBundleId`, optional `TemporalWorksetBundleId`,
`GeometrySnapshotId`, optional `TemporalGeometrySnapshotId`,
`StateSnapshotId`, time stage, model revisions, and any
`LimiterSnapshotId`. A residual/Jacobian pair cannot replace one member of this
tuple between calls.

```cpp
ResidualResult evaluate_matrix_free_residual(
    const OperatorContext &, ConstStateView x, StateView r);

ResidualResult apply_matrix_free_exact_jacobian(
    const LinearizationContext &, ConstStateView direction, StateView image);

ResidualResult evaluate_assembled_reference_residual(
    const OperatorContext &, ConstStateView x, StateView r);

AssemblyResult assemble_exact_reference_jacobian(
    const LinearizationContext &, SerialBlockSparseMatrixView J);

PreconditionerResult build_approximate_preconditioner(
    const PreconditionerContext &, ApproximateOperatorView P);
```

“Exact” means exact for the chosen discrete residual at a fixed
`GeometrySnapshot`, fixed `TemporalGeometrySnapshot`, fixed `SpaceSnapshot`,
and fixed time-stage coefficients.
The safeguarded quasi-Newton method does not differentiate through cut
quadrature construction, classification, normal reconstruction, swept-domain
construction, or other spatial/temporal geometry updates. Within that stated
linearization, the matrix-free Jacobian
action and assembled Jacobian must represent the same derivative. A kernel may
provide an explicit tangent evaluation or use a scalar-generic directional
type; finite differences are a diagnostic, not the production definition.
If the configured residual contains a nonsmooth limiter, the linearization
context additionally records the exact `LimiterSnapshotId` and its
declared semismooth active-branch or differentiated tangent. Numerically frozen
edge factors are an approximation and cannot enter this exact operation.

The initial assembled residual can use distributed deal.II vectors, but the
assembled Jacobian reference is intentionally a bounded-size, one-rank test
facility backed by deal.II's native sparse matrices. Rift does not claim a
scalable distributed assembled matrix without PETSc, Trilinos, or a future
custom owner-row implementation.

The preconditioner is not allowed to masquerade as that derivative. It has its
own interface, configuration, cache keys, and tests, and may omit couplings,
freeze material properties, simplify geometry, or use lower-order operators.
This distinction keeps convergence tuning from silently changing the nonlinear
problem. See [Execution backend](17-execution-backend.md) for the storage and
traversal implementations and [Nonlinear coupling](20-nonlinear-coupling.md) for
the frozen-geometry linearization contract.

## Invariants and invalidation

Every operator call satisfies these invariants:

- input and output state layouts match the referenced space epoch;
- every workset record belongs to that space and geometry snapshot;
- all interface normals use the canonical phase-pair orientation;
- quadrature weights and points are identical across exact backends;
- all active fictitious-band unknowns receive either physical or extension/
  stabilization equations; and
- any prepared limiter and temporal workset match the evaluation context; and
- kernels do not retain evaluator views after the call.

A mesh or space change invalidates all evaluators and worksets. A geometry
change invalidates cut, interface, and geometry-dependent stabilization
worksets, as well as any linearization or preconditioner built from them. A
state change invalidates cached constitutive values unless their cache key
contains the new `StateSnapshotId`. It does not by itself rebuild structural
worksets. Backend objects reject identity mismatches instead of attempting a
best-effort evaluation.

## Verification obligations

Small deterministic meshes exercise every entity kind, including cut cells,
hanging faces, MPI partition boundaries, and both orientations of a material
interface. Required tests compare assembled and matrix-free residuals; compare
`apply_matrix_free_exact_jacobian(v)` with the assembled product `J*v`; and verify the
directional residual difference over a convergence range. Interface tests sum
both phase contributions to expose sign or orientation errors. SIMD results are
compared with scalar-lane results, and MPI tests compare partitioned and serial
integrals. Cache tests intentionally advance each epoch and require stale
objects to fail loudly. Conservation and mesh-transfer tests are specified in
[Adaptivity and transfer](08-adaptivity-and-transfer.md); time-stage
admissibility is specified in [stabilization and
admissibility](12-stabilization-and-admissibility.md).

The backend should use deal.II's
[`MatrixFree`](https://dealii.org/developer/doxygen/deal.II/group__matrixfree.html)
facilities where they fit, while preserving this higher-level contract rather
than exposing their iterators to physics code.
