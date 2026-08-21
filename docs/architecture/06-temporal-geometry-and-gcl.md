---
title: Temporal geometry and GCL
description: Swept cut-domain, stage-quadrature, and geometric-conservation contracts for moving interfaces.
---

# Temporal geometry and GCL

A sequence of correct spatial cuts is not by itself a conservative moving-domain
discretization. If the physical part of a background cell changes between time
stages, simply replacing one cut mass matrix with another creates or removes a
constant state. Rift therefore treats temporal geometry as an explicit input to
the time-discrete residual.

`TemporalGeometryService` derives swept-volume and moving-interface data from a
fixed `SpaceSnapshot`, the relevant level-set state snapshots, their spatial
`GeometrySnapshot`s, the interface kinematics, and the selected time method. It
does not own the level-set fields or choose an interface law.

## Conservative reference

The reference is the space-time balance on each swept phase region. Its lateral
material-interface flux uses

```text
Fα · n − VΓ Uα.
```

The same `VΓ` must occur in the [level-set system](04-level-set-system.md), the
interface exchange, and the swept-volume calculation. The implementation may
evaluate a genuine space-time cut quadrature or an algebraically equivalent
stage/swept-volume construction, but it must reproduce this reference balance.

For a constant field with no physical boundary or source contribution, the
discrete change of phase volume and the integrated mesh/interface-motion term
must cancel to the stated tolerance. This is Rift's discrete geometric
conservation law (GCL). It is a required residual identity, not merely an
after-step diagnostic.

## Snapshot contract

```cpp
struct TemporalGeometryRequest {
  const SpaceSnapshot &space;
  StateSnapshotId step_start_state;
  GeometrySnapshotId step_start_geometry;
  std::span<const StateSnapshotId> stage_states;
  std::span<const GeometrySnapshotId> stage_geometries;
  TimeMethodDescriptor method;
  InterfaceKinematicsHistory kinematics;
};

class TemporalGeometrySnapshot {
public:
  TemporalGeometrySnapshotId id() const;
  SpaceEpoch space_epoch() const;
  SweptVolumeView swept_volumes(PhaseId) const;
  TemporalInterfaceQuadratureView interface_rule(InterfaceId) const;
  ActivationMeasureView activation_terms(PhaseId) const;
  GclReport verify_constant_state() const;
};
```

Every input state and geometry identity is recorded in the snapshot. A trial
stage gets a private temporal-geometry identity, so rejected line-search or
time-step candidates cannot alias accepted swept-volume caches. A change to any
stage level set, interface speed, Runge–Kutta coefficient, time interval,
background mapping, or space epoch invalidates the snapshot.

The temporal snapshot contains measures and quadrature only. Phase and
interface kernels still supply physical fluxes. The execution backend combines
them under the time-integrator coefficients and scatters the resulting mass,
flux, interface, and activation terms exactly once. `WorksetRouter` derives a
separately stamped `TemporalWorksetBundle` from this snapshot; a spatial
workset bundle alone cannot route swept activation.

## IMEX-ARK and Radau use

For the production IMEX-ARK method, every operator term declares both an
explicit/implicit partition and its geometry-time treatment. A stage residual
cannot combine an explicit physical term evaluated on one cut with an implicit
mass or interface term evaluated on another undocumented cut. Paired split
terms must sum to the unsplit space-time reference under the stage quadrature.

The stiffly accurate final stage supplies the candidate endpoint geometry.
Embedded error estimation includes scaled geometry-field and interface-position
components as well as physical state blocks. On rejection, all stage geometry
and temporal-geometry snapshots are discarded with the state stages.

The fully implicit Radau IIA verification path constructs the corresponding
coupled-stage temporal snapshot from the same spatial geometry service. It is
used to verify temporal order, splitting error, and swept-volume conservation;
it does not define a second geometry model.

## Newly occupied fragments

When an interface sweeps into a subcell whose phase was absent at the start of
the step, the temporal residual uses the phase's already allocated
fictitious-band unknowns and the matching [phase-activation
policy](07-phase-activation.md). It does not invent an old physical value or
activate a new DoF inside the stage. The activation measure in
`TemporalGeometrySnapshot` accounts for the subcell's entry into the physical
domain and is paired with the moving-interface flux. The activation policy
supplies the state/history consumed by that one balance; it does not add a
second conserved source.

If the swept region reaches outside the fixed support envelope, temporal
geometry returns `needs_space_rebuild`. The solver rolls back to the accepted
time level, rebuilds the envelope, and retries the entire time step. If the
swept slab contains an unsupported junction or contact line, construction
returns a terminal topology error.

## Responsibilities and invariants

The service must:

- use the canonical minus/plus interface orientation;
- integrate phase-volume change and interface motion with compatible temporal
  order;
- provide one ownership rule for swept fragments crossing MPI partitions;
- preserve the exact residual's scalar and mixed-precision contracts;
- report time-quadrature, reconstruction, and GCL defects by phase and cell;
- keep temporal geometry immutable during one residual/Jacobian linearization.

It does not reinitialize the level set, transfer state between meshes, select a
flux, or repair a failed GCL audit by changing phase totals. An approximate
preconditioner may use frozen stage geometry or lower-order swept terms only
when that approximation is explicit in its specification.

## Required tests

1. Constant-state free-stream preservation for translating planar interfaces.
2. Exact volume ledger for manufactured normal motion and zero physical flux.
3. Conservation while an interface crosses a background vertex, hanging face,
   and MPI partition.
4. Agreement between space-time quadrature and the production stage
   construction on polynomial-in-time motions.
5. IMEX split-sum and Radau temporal-order tests on identical geometry history.
6. Equality of the `VΓ` used by level-set transport, phase relative fluxes, and
   swept-volume terms.
7. Rejection of stale state, geometry, time-method, and space identities.
8. Rollback after GCL, topology, or envelope failure with the accepted residual
   still evaluable.

The [solver lifecycle](22-solver-lifecycle.md) owns construction and disposal of
these snapshots at each time-stage attempt.
