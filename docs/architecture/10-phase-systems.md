---
title: Phase systems
description: Contract for the runtime PhaseSystem facade and its statically compiled phase kernels.
---

# Phase systems

A `PhaseSystem` represents one phase's discrete physics inside the coupled solver. It is a facade over a registered, statically compiled implementation; it is **not** an independently advancing solver. The stage coordinator owns time integration, the nonlinear solve, rollback, and acceptance. The [runtime phase graph](01-phase-graph.md) owns which phases exist and which interfaces join them. [Geometry and topology](03-geometry-topology.md) owns where a phase is physically present, and [typed worksets](05-worksets.md) carry the classified batches on which kernels execute.

This boundary keeps domain discovery out of the physics while preserving static dispatch in quadrature loops. Configuration selects a compiled implementation once per phase. Calls through the facade then dispatch once per workset, never once per quadrature point or species.

## Responsibilities

A phase system is responsible for:

- declaring its PDE category and primary field layout;
- declaring the phase's block metadata and receiving non-owning views of the
  central vectors and constraints described in [discrete state and DoF ownership](02-discrete-state.md);
- holding the global-to-local species map and runtime material/mechanism data;
- applying mass, advection, diffusion, source, constraint, and phase-local stabilization operators on supplied worksets;
- declaring a compatible `StabilizationAdmissibilityPolicy` for transported CG
  fields and a `PhaseActivationPolicy` for moving support, or an explicit
  capability proving that physical activation cannot occur;
- exporting a canonical `InterfaceTrace` for an [interface operator](14-interface-operators.md);
- accepting an `InterfaceExchange` and scattering it into the correct phase residual blocks;
- exporting one-sided data required by [physical boundary operators](13-boundary-operators.md);
- reporting admissibility, conservation, entropy, EOS, and constitutive diagnostics;
- describing exact Jacobian actions and the low-order or patch surrogates available to a preconditioner.

The supported state families have different blocks. A fully compressible fluid
stores phase-local partial densities, momentum, and total energy. A general-EOS
low-Mach fluid instead stores phase-local partial densities, momentum,
enthalpy, and hydrodynamic pressure. It declares its thermodynamic-pressure
capability to the [regional constraint system](15-regional-constraints.md), which
uses prescribed `p₀(t)` for an open region or allocates one solved `p₀,r`
unknown and compatibility row for a closed region. Strict and quasi-incompressible fluids
expose their pressure/volume constraint. An Eulerian solid exposes its
reference map, momentum, thermal variable, transported history, and optional
solid pressure. A zero-sized block is absent; it is not represented by dummy
unknowns.

## Non-responsibilities

A phase system does not:

- search the mesh or decide which cells belong to the phase;
- construct cut quadrature or mutate a level set;
- choose neighboring phases or an interface law;
- advance its state with a private time step;
- perform a phase-local Newton solve that ignores interface and global constraints;
- invent values when support is activated;
- hide clipping or an after-stage limiter outside the documented residual;
- impose exterior boundary conditions inside a material-interface kernel;
- own global surface species, surface mass, or surface energy in the initial model.

These exclusions matter. Low-Mach compatibility is a connected-region reduction, interface exchange is intrinsically two-sided, and moving-support conservation is a stage-wide property. Hiding any of them inside a nominally self-contained phase solver would make the monolithic residual impossible to audit.

## Runtime facade and compiled registry

The runtime object is deliberately small:

```cpp
class PhaseSystem {
public:
  PhaseId id() const noexcept;
  PhysicsKind physics_kind() const noexcept;
  const FieldLayout &fields() const noexcept;
  const PhaseSpeciesMap &species() const noexcept;

  void apply(OperatorPart part,
             const WorksetHandle &workset,
             const StageContext &stage,
             const StateView &src,
             ResidualView dst) const;

  InterfaceTraceHandle make_interface_trace(
      const InterfacePointHandle &point,
      const StateView &state) const;

  void scatter_interface_exchange(
      const InterfacePointHandle &point,
      const InterfaceExchangeHandle &exchange,
      ResidualView dst) const;

  AdmissibilityReport check(const StateView &state) const;
  BlockDescription blocks() const;
};
```

`WorksetHandle`, `StateView`, and the trace handles are type-erased only at the coarse boundary. A registry key selects a concrete implementation such as

```cpp
PhaseKernel<dim,
            ContinuousGalerkin,
            CompressibleState,
            ModelBundle<MyEos, StefanMaxwell, MyChemistry>,
            VectorizedArray<double>>;
```

The registry rejects unavailable combinations during configuration. It must not silently fall back to a slower virtual quadrature kernel or to altered physics. The initial spatial implementation is continuous Galerkin. The registry and contracts reserve a discretization tag so DG can be added without placing DG face fluxes or limiters inside the CG kernel.

## Workset-level dispatch

The facade accepts already classified work. Regular volume batches, cut-volume fragments, ordinary interior faces, physical exterior boundaries, material-interface fragments, ghost faces, and global reductions are different workset kinds. The registry visitor performs one switch for a homogeneous batch and calls a fully typed inner kernel. Runtime material arrays and phase-local species spans remain data, while dimension, scalar backend, PDE state family, and compiled policy family remain template parameters.

An operator invocation also declares whether it belongs to the explicit or implicit partition and whether it supplies residual, Jacobian-vector, transpose, or preconditioner-surrogate action. The exact residual and Jacobian action must use the same model data and geometry version. Lagged coefficients belong only to a named approximate operator.

## Lifecycle

Construction proceeds through `configure`, `finalize_layout`, and registry
binding. A geometry-stage transition supplies immutable worksets and
connected-region labels. A nonlinear base-point change may rebuild and regroup
geometry-dependent worksets, but it may not change field layout, active finite
elements, connected-region unknown count, or the maximum coupling graph fixed
by `SpaceSnapshot`. One residual/Jacobian linearization pair always uses one
unchanged workset bundle. Only endpoint or accepted-state transaction
acceptance commits state and transported history; a nonlinear base-point
promotion remains private, and rejection restores all phase data to the
checkpoint.

Cache keys include phase id, material-data revision, `WorksetBundleId`,
optional `TemporalWorksetBundleId`, `GeometrySnapshotId`, optional
`TemporalGeometrySnapshotId`, time stage, and `StateSnapshotId`. An EOS inverse, chemistry factorization, or
constitutive tangent computed for another identity is invalid.

## Invariants and failure behavior

- Phase-local species occupy compact indices; absent species have no bulk DoF.
- Summing species residuals recovers the phase mass residual.
- Formation energy is counted by the thermodynamic model and never added again as an independent reaction-heat source.
- An interface exchange is scattered once, with the orientation supplied by the interface operator.
- Matrix-free and assembled reference actions evaluate the same mathematical residual.
- Invalid density, temperature, EOS branch, deformation determinant, constraint, or local solve produces a structured failure. It never triggers clipping or a hidden reduced model.
- A phase system cannot accept a workset whose phase, geometry snapshot, field
  layout, or scalar backend does not match.

## Required tests

1. Registry tests cover every supported key and reject unsupported combinations with a diagnostic listing available implementations.
2. Each state family passes field-layout, serialization, zero-sized-block, and phase-local-species tests.
3. Constant and manufactured states compare assembled residuals with scalar and SIMD matrix-free actions.
4. AD and finite-difference directional derivatives agree with the analytic Jacobian action on a fixed workset.
5. Summed species, momentum, and energy residuals pass phase-balance tests.
6. Boundary traces and interface traces reconstructed from the same state agree on common thermodynamic quantities.
7. Cache invalidation tests change geometry, mechanism data, and Newton state independently.
8. A mocked stage rejection restores state, history, connected-pressure data, and diagnostics exactly.

The physical policy types used inside each compiled implementation are
specified in [Model policies](11-model-policies.md). The discretization-level
positivity and limiting contract is in [stabilization and
admissibility](12-stabilization-and-admissibility.md), and moving-region birth
data is in [phase activation](07-phase-activation.md).
