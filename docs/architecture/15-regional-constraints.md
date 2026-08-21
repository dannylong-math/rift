---
title: Regional constraints
description: Connected-region global unknowns, compatibility rows, gauges, and nullspace ownership.
---

# Regional constraints

Some equations are associated with a connected physical region rather than a
cell. General-EOS low-Mach flow needs a thermodynamic-pressure decision for
each connected region; incompressible pressure may need a regional gauge; and
an unconstrained solid may expose rigid modes. These contracts cannot be
assembled independently by phases, boundaries, and the linear solver.

`RegionalConstraintSystem` is the single owner of the global schema and its
residual rows. `GeometryTopologyManager` supplies connectivity,
`BoundaryOperatorRegistry` supplies the boundary classification, and each
`PhaseSystem` supplies capabilities and local integrands. The constraint
system combines them once and publishes semantic blocks in the central
`StateStore`.

## Schema construction

```cpp
struct RegionalConstraintSchema {
  RegionId region;
  PhaseId phase;
  RegionBoundaryKind boundary_kind;
  std::vector<GlobalUnknownDescriptor> unknowns;
  std::vector<GlobalResidualDescriptor> equations;
  GaugeDescriptor gauge;
  NullspaceDescriptor nullspace;
};

class RegionalConstraintSystem {
public:
  RegionalSchemaSet build_schema(
      const PhaseGraph &,
      const GeometrySnapshot &accepted_geometry,
      const BoundaryOperatorRegistry &,
      const PhaseCapabilityRegistry &) const;

  void apply(const GlobalReductionWorkset &,
             const EvaluationContext &,
             ConstStateView,
             ResidualView) const;
};
```

Schema construction occurs before the corresponding `SpaceSnapshot` is
published. The ordered set of `RegionId`s, global blocks, rows, gauges, and
nullspace descriptors is immutable within that `SpaceEpoch`. Global unknowns
are semantic state entries; they are not mutable members hidden inside a phase
object.

The geometry service assigns deterministic region identities for one accepted
topology and supplies a connectivity signature. A same-space geometry update
must provide a verified bijection to those identities. Breakup, coalescence, or
component appearance/disappearance has no implicit scalar remap and is an
unsupported layout-changing transition initially.

A topology-preserving refinement, coarsening, or repartition transaction also
supplies an old-to-target region bijection based on physical connectivity, not
cell or rank identity. `RegionalStateTransfer` uses it to copy and audit solved
global scalars and every required time history before the target state layout
is committed. A non-bijective result is not ordinary h-adaptation.

## Low-Mach regions

For a general-EOS low-Mach phase, every connected region is classified as
open or closed according to its physical boundary policies and configured
pressure reference:

- An open region with prescribed thermodynamic pressure takes `p₀(t)` from its
  boundary/configuration data and allocates no solved `p₀` block.
- A closed region allocates one spatially uniform `p₀,r` unknown and one
  compatibility row derived from the integrated EOS/divergence constraint.
- A hydrodynamic-pressure field `π` receives either a compatible physical
  reference or one declared mean/gauge condition per unconstrained region.

The closed-region row includes the stage-consistent thermodynamic-pressure
rate, boundary volume/mass flux, heat and species contributions, and any other
term required by the phase's exact low-Mach formulation. `ṗ₀` is computed from
the selected IMEX or Radau stage coefficients and retained state history; it is
not estimated by a phase-local finite difference. Boundary operators publish
their flux contributions using the same sign and units as the regional
reduction.

The constraint system validates that each region is neither under- nor
over-specified. Prescribing `p₀` while also allocating a closed-volume
compatibility unknown, or omitting both pressure reference and gauge, is a
configuration failure.

## Other pressure and rigid modes

Strict and quasi-incompressible phases register their pressure/volume
constraint and gauge requirements. Nearly incompressible solids may register a
solid-pressure gauge. Unconstrained solid mechanics registers rigid
translations and rotations rather than inventing pressure conditions.

The resulting `NullspaceDescriptor`s are the authoritative input to
[preconditioning services](19-preconditioning.md) and Krylov projection. Boundary
conditions may remove modes; that removal is decided during schema
finalization and tested, not inferred by a multigrid smoother.

## Reduction and storage

`GlobalReductionWorkset`s provide cut-volume, boundary, and interface
integrands grouped by `RegionId`. Each geometrical contribution has one MPI
owner, local sums are reduced deterministically within floating-point
tolerance, and the resulting row is scattered to the global block owner.
Small global blocks may use an owner-rank storage adapter, but they remain part
of the coupled vector concept and participate in norms, rollback, checkpoint,
Jacobian actions, and assembled reference tests.

The exact residual and Jacobian action use identical region measures,
thermodynamic derivatives, stage coefficients, and boundary fluxes. A
preconditioner may approximate their Schur coupling, but it cannot remove a
compatibility row from the nonlinear problem.

## Lifecycle and failures

A new accepted geometry may change region measures without changing the
schema; this rebuilds its reduction worksets and geometry-dependent
coefficients. A new schema, region-id set, gauge, or global-block count requires
a new `SpaceEpoch`. Reinitialization must preserve the region topology unless a
future layout-transition transaction is active.

Failures name the phase, `RegionId`, geometry snapshot, boundary assignments,
and exact missing or duplicate condition. MPI ranks agree on the schema hash
before vectors are allocated. A region mismatch during evaluation is a stale
space error, not a request to manufacture a row.

## Required tests

1. Open low-Mach regions allocate prescribed `p₀` data but no solved scalar.
2. Closed low-Mach regions allocate exactly one `p₀,r` unknown and compatibility
   row per component.
3. Stage `ṗ₀` and its Jacobian agree with IMEX and Radau coefficients.
4. Pressure gauges and solid rigid modes appear or disappear under the intended
   boundary conditions.
5. Regional reductions agree across serial and repartitioned MPI meshes.
6. Cut-volume and boundary-flux manufactured tests satisfy the compatibility
   row and conservation ledger.
7. Duplicate pressure prescriptions, missing gauges, stale `RegionId`s, and
   non-bijective topology changes fail explicitly.
8. Null modes are reproduced on every multigrid level and projected with the
   declared block-scaled inner product.
9. Checkpoint and rollback restore all regional values and controller history.

The field/block placement is defined by [discrete state and DoF
ownership](02-discrete-state.md), while temporal acceptance is defined by [time
integration](21-time-integration.md).
