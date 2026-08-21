---
title: Preconditioning services
description: Block factorization, native deal.II multigrid, cut-patch corrections, and nullspace contracts.
---

# Preconditioning services

The exact nonlinear residual and Jacobian action define the problem;
preconditioners are named approximations to that problem. `PreconditionerService`
builds and applies those approximations without changing any exact kernel. Its
first target is MPI plus CPU SIMD using deal.II-native distributed vectors and
matrix-free geometric multigrid. PETSc and Trilinos are not required.

## Block plan

`StateLayout` supplies semantic blocks rather than positional ranges. A
`PreconditionerPlan` groups them into transported composition/thermal fields,
momentum or velocity, pressure-like constraints, level-set geometry, locally
stiff variables, global regional unknowns, and cut/interface patches.

```cpp
struct PreconditionerSpecification {
  BlockFactorizationKind factorization;
  MultigridPolicy multigrid;
  PatchPolicy cut_patches;
  LocalSolvePolicy local_solves;
  PrecisionPolicy precision;
};

class PreconditionerService {
public:
  PreconditionerHandle build(
      const LinearizationContext &exact_base,
      const PreconditionerSpecification &,
      const NullspaceCatalog &);

  ApplyReport apply(ConstStateView rhs, StateView correction) const;
};
```

The baseline right preconditioner is block triangular or multiplicative:

1. local or clustered chemistry/constitutive solves;
2. local condensation or approximation of interface closure variables;
3. a species-coupled thermal/transport solve;
4. a hydrodynamic or elastic saddle-block solve;
5. a level-set transport/extension solve; and
6. restricted cut/interface patch corrections followed by an optional second
   local correction.

An additive form is allowed as a cheaper, more parallel configuration. The
outer Krylov implementation must permit a variable preconditioner because
inner iteration counts, mixed precision, chemistry clusters, and patch
factorizations may vary. FGMRES is the leading initial candidate; the concrete
method remains an implementation selection.

## Native multigrid hierarchy

Uniform solution degree and adaptive `h` refinement define the initial spaces;
they do not forbid global polynomial coarsening inside the solver hierarchy.
The default hierarchy first applies `p → p − 1 → … → 1` on each mesh level,
then follows the adaptive `h` levels. Each phase field group and the
full-background level-set group has a declared transfer path. Several
`DoFHandler`s may contribute separate level operators, combined by the block
plan. Transfers preserve constraints, phase-envelope semantics, and the
nullspace modes declared below.

The bulk level operators are matrix-free and use deal.II distributed vectors.
Chebyshev/Jacobi smoothing is suitable only for benign scalar elliptic blocks.
Coupled species diffusion uses a species-block smoother that preserves the
zero-summed diffusive-flux constraint. Pressure-constrained fluids and nearly
incompressible solids use Vanka, vertex/element patch, distributive, or
augmented-Lagrangian smoothing according to their registered capability.

`FE_Nothing` and arbitrarily small cuts make a plain background hierarchy
insufficient. Rift composes the bulk cycle with additive or multiplicative
Schwarz corrections on macroelements covering cut cells, Nitsche couplings,
ghost penalties, and semi-implicit capillary terms. Candidate-interface cells
are never omitted from those patches. A coarse approximation retains
coefficient jumps, species constraints, ghost stabilization, and a declared
low-order interface/surface term.

The coarsest solve initially uses deal.II-native iterative services on the
distributed vector. A bounded coarse system may be gathered and solved on a
designated rank only under an explicit size threshold and broadcast contract;
this is not presented as a scalable assembled matrix. Failure to reach the
coarse tolerance is reported to the flexible outer method.

## Physics services

Chemistry and local constitutive variables use dense or sparse local
factorizations. A clustering policy may share a representative Jacobian among
nearby thermochemical states, but cluster error indicators must refresh or
split it. Clustering changes only the preconditioner.

Stefan–Maxwell diffusion is elliptic on a composition constraint subspace and
dense in species space. Its smoother uses a reduced basis or augmented local
saddle block. Independent scalar species cycles are not a conforming default
because they discard the dominant cross-diffusion structure.

Momentum/heat blocks may lag enthalpy-diffusion, Dufour/Soret,
viscosity-temperature, or EOS cross derivatives. A low-Mach or incompressible
block includes its pressure constraint and global regional rows; an Eulerian
solid block includes its elastic-volume constraint where present. These
omissions and approximations are part of the specification hash.

Regional thermodynamic pressures form a small dense arrowhead coupling to
their phase blocks. The first low-Mach preconditioner eliminates or applies the
`p₀,r` Schur complement exactly at that small global level after the required
MPI reductions; treating those rows as unrelated diagonal scalars is
nonconforming. The regional constraint capability supplies the dense coupling
and its nullspace/gauge metadata.

For quasi-incompressible mixtures, the pressure/volume Schur approximation
must retain the leading composition-mediated term:

```text
Sp ≈ Dp − Cz Azz⁻¹ Azp.
```

A registered reduced, patch, or multigrid application of `Azz⁻¹` may be used,
but silently dropping this coupling is not the baseline approximation. The
capability descriptor states which pressure/composition approximation and
constraint subspace it implements.

Interface algebraic variables are condensed locally. The patch correction
retains the induced bulk coupling, capillary surface term, Nitsche terms, and
ghost stabilization to the configured approximation order. It consumes the
same phase-pair orientation and patch identities as the exact operator.

The interface service also declares an escalation ladder for strong added-mass,
mixed-pressure, or capillary coupling. It may start with an additive cut patch,
then select multiplicative bulk/interface application, a larger macroelement
factorization, or an explicitly assembled condensed interface Schur when
measured coupling and outer convergence cross configured thresholds. Escalation
changes only the preconditioner specification; it never duplicates interface
fluxes or changes the exact residual.

## Nullspace and global-mode contract

Every phase physics and global constraint provider contributes a
`NullspaceDescriptor`. Possible modes include:

- one pressure constant per unconstrained connected fluid region;
- rigid translations and rotations for an unconstrained solid;
- constant modes for diffusion and level-set extension operators; and
- species-constraint modes in the selected Stefan–Maxwell formulation.

The `NullspaceCatalog` orthonormalizes modes in the configured block-scaled
inner product, identifies modes removed by boundary conditions or gauges, and
defines projection of Krylov right-hand sides and corrections. Multigrid
transfer must reproduce every surviving mode on every level. A connected-region
measure or topology-preserving label refresh rebuilds the catalog through its
verified `RegionId` bijection. A change in global unknown count would require a
new `SpaceEpoch` under the deferred layout-transition contract and is terminal
initially. Missing or linearly dependent required modes are hierarchy
construction errors, not reasons to increase Krylov iterations.

## Precision, identity, and rebuilding

The service may store level coefficients, patch matrices, and inner vectors in
`PreconditionerNumber`, independently of `StateNumber`. Conversion happens only
at the apply boundary. Variable precision or inner tolerance requires a
flexible outer method and is recorded in diagnostics.

A handle is keyed by `SpaceEpoch`, `BackendBindingId`, `WorksetBundleId`,
optional `TemporalWorksetBundleId`, `GeometrySnapshotId`, `StateSnapshotId`,
optional `TemporalGeometrySnapshotId`, time-stage data, model-data revision,
approximation specification, nullspace-catalog revision, and any
`LimiterSnapshotId` whose frozen factors it consumes. The plan may deliberately
lag selected state or geometry inputs, but its key records the source actually used. A mesh,
envelope, topology-preserving regional remap, or multigrid-transfer change
rebuilds the hierarchy atomically. A non-bijective regional change is terminal
until the future layout-transition transaction exists. Rejected candidates never overwrite the usable
preconditioner for the current nonlinear base.

## Required tests

1. Exact-residual convergence is unchanged when the preconditioner is replaced
   or disabled.
2. Every null mode is detected, transferred, projected, or fixed by a declared
   gauge.
3. Iteration counts remain cut-position robust on shrinking phase fragments.
4. `h`-refinement, hanging nodes, repartition, and multiple `DoFHandler`s
   produce valid level transfers.
5. Species-block smoothers preserve their constraint and outperform the
   deliberately weak independent-scalar reference on coupled cases.
6. Cut-patch tests include capillary, Nitsche, and ghost couplings without
   adding physical cross-interface transport.
7. Float and double preconditioner paths converge to the same exact residual
   tolerance where conditioning permits.
8. Fault injection in hierarchy, coarse, cluster, and patch construction leaves
   the previous accepted handle usable.
9. MPI weak-scaling tests report setup, communication, memory, and apply costs
   separately from exact operator costs.
10. Closed low-Mach tests compare the exact small dense `p₀` Schur with the
    preconditioner application.
11. Quasi-incompressible tests isolate the composition-mediated pressure term
    and reject a capability that declares it but omits it.
12. Added-mass and mixed-pressure interface tests trigger each escalation level
    and verify improvement without changing the exact residual.

The scalar conversion boundary is defined in [linear algebra and mixed
precision](18-linear-algebra.md); lifecycle ownership and retry behavior are in
[solver lifecycle](22-solver-lifecycle.md).
