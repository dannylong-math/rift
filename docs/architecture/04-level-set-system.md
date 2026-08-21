---
title: Level-set system
description: Coupled full-background geometry fields, interface-speed routing, transport, and stabilization contracts.
---

# Level-set system

`LevelSetSystem` is the equation system that evolves Rift's implicit geometry.
It is distinct from `GeometryTopologyManager`: the former contributes equations
to the coupled residual, while the latter reconstructs immutable geometry from
a supplied field-set snapshot. Neither phase system nor interface operator may
advance the geometry privately.

The level-set unknowns live on the complete background mesh and are ordinary
global blocks in the central `StateStore`. They participate in the same time
stages, safeguarded quasi-Newton solve, error control, checkpoint, and rollback
as the phase fields. This is the ownership boundary that makes movement of the
interface part of the monolithic problem rather than an after-the-fact update.

## Field set and phase encoding

The public contract uses a `LevelSetFieldSet`, not one hard-coded scalar. A
two-region problem may use one signed component. A general phase graph may use
several components, provided a registered `GeometryEncodingPolicy` maps them
unambiguously to `PhaseId`s and declared pairwise `InterfaceId`s.

```cpp
struct LevelSetSchema {
  GeometryEncodingId encoding;
  std::vector<GeometryFieldId> components;
};

class GeometryEncodingPolicy {
public:
  PhaseClassification classify(
      const LevelSetPointView &) const;
  InterfaceIdentity identify_interface(
      const LevelSetNeighborhoodView &) const;
  EncodingValidation validate(const PhaseGraph &) const;
};
```

The encoding type is chosen once through the compiled registry; its coefficients
and phase/interface maps are runtime data. The policy must represent the
configured graph, give deterministic labels away from measure-zero ties, and
report an unsupported junction instead of resolving it by arbitrary priority.
Changing the encoding schema changes the state layout and starts a new
`SpaceEpoch`.

## Coupled transport contract

For every transported component, the baseline equation is an advection law on
the full background mesh:

```text
∂t φα + vΓ,ext · ∇φα = 0.
```

Only the normal component changes a represented zero contour. On each material
interface, the extension must satisfy

```text
vΓ,ext · n = VΓ,
```

where `VΓ` is the normal speed returned by the same local interface closure
that supplies phase mass, momentum, species, and energy exchange. An
`InterfaceOperator` therefore publishes one immutable
`InterfaceKinematicsView` alongside its side exchanges. The level-set system
consumes that view; it never recomputes phase-change kinetics or infers speed
from one phase alone.

A registered `VelocityExtensionPolicy` extends the interface-normal data into
the full-background or narrow-band transport velocity. Closest-point, elliptic,
or hyperbolic extension algorithms may be supplied. The policy declares its
stencil radius, tangential-velocity convention, differentiability, and failure
conditions. Its stencil contributes to the support-cushion calculation even
though the geometry fields themselves exist everywhere.

Exterior conditions for geometry fields are selected by a separate
`GeometryFieldBoundaryPolicy` and evaluated on
`GeometryFieldBoundaryWorkset`s. Typical policies prescribe a far-field phase
encoding on inflow or use a compatible outflow/extrapolation condition. They
do not impose contact angle, wetting, or line motion. The initial topology
guard requires every represented material interface and its cushion to remain
away from the exterior boundary; reaching it is the terminal contact-line
event defined by the geometry manager.

## Conceptual operator API

```cpp
class LevelSetSystem {
public:
  const LevelSetSchema &schema() const;

  void apply_transport(
      OperatorPart,
      const GeometryFieldVolumeWorkset &,
      const EvaluationContext &,
      ConstLevelSetFieldSetView source,
      const ExtendedInterfaceVelocityView &velocity,
      LevelSetResidualView destination) const;

  void apply_exterior_boundary(
      const GeometryFieldBoundaryWorkset &,
      const EvaluationContext &,
      ConstLevelSetFieldSetView source,
      LevelSetResidualView destination) const;

  ExtensionResult extend_interface_velocity(
      const InterfaceKinematicsWorkset &,
      const EvaluationContext &,
      const InterfaceKinematicsView &closure_kinematics,
      ExtendedInterfaceVelocityView destination) const;
};
```

`GeometryFieldVolumeWorkset` covers the complete background mesh.
`InterfaceKinematicsWorkset` carries the oriented interface points and their
shared quadrature identity. The matching evaluation context binds the
`InterfaceKinematicsView` returned by the single closure evaluation at those
points. Geometry stabilization uses explicitly tagged background-face or patch
worksets; it is not mixed with physical phase ghost penalties.

The transport and extension kernels are scalar-generic and provide the same
exact residual, Jacobian action, and assembled reference contracts as phase
operators. Runtime dispatch occurs once per homogeneous workset. Stabilization
may use SUPG, CIP, or a declared entropy-viscosity policy, but an approximate
preconditioner is not allowed to change the accepted transport equation.

## Coupling and lifecycle

At a nonlinear base point, the coordinator performs a coupled evaluation:

1. reconstruct geometry from the base `LevelSetFieldSet`;
2. evaluate phase traces and one local closure on each interface point;
3. publish its `InterfaceExchange` and `InterfaceKinematicsView` together;
4. extend `VΓ` according to the configured policy;
5. evaluate level-set transport and all phase/interface residual blocks against
   the same state, geometry, and temporal-geometry snapshots.

This order is an evaluation dependency, not a staggered time advance. The
level-set block remains in the global nonlinear vector, and its Jacobian action
includes the declared dependence of the extension and `VΓ` on phase traces.
The safeguarded quasi-Newton geometry contract may omit derivatives of the
reconstruction algorithm itself, but it must not silently lag the algebraic
interface closure in the exact frozen-geometry residual.

Reinitialization is a separate accepted-state transaction. It changes the
field-set representation without advancing physical time and follows the
[level-set reinitialization](09-level-set-reinitialization.md) audit. Mesh
adaptation transfers all components before rebuilding geometry.

## Invariants and failure behavior

- Every realized pairwise interface receives exactly one `VΓ` from its selected
  interface closure.
- The speed used in level-set transport is the speed used in the relative phase
  flux and swept-geometry calculation.
- A field-set snapshot and its reconstructed geometry identify each other by
  `LevelSetFieldSetSnapshotId` and `GeometrySnapshotId`.
- Full-background geometry blocks never select `FE_Nothing`.
- Extension equations are well posed on every requested stencil and do not
  create physical phase transfer.
- Ambiguous labels, inconsistent speeds, unsupported encoding/junctions,
  extension failure, or non-finite geometry fields reject the nonlinear trial.

Required tests include rigid translation and rotation of planar and curved
interfaces; phase-change motion with unequal normal fluid velocities;
scalar/SIMD and assembled/matrix-free agreement; derivative checks coupling
phase traces to `VΓ` and the level-set residual; deterministic multiphase
classification under MPI repartitioning; extension-stencil cushion tests; and
rollback tests proving that a rejected geometry candidate changes no live
state.

Stage-to-stage conservation of moving cut volumes is governed by
[temporal geometry and GCL](06-temporal-geometry-and-gcl.md), not by the transport
operator alone.
