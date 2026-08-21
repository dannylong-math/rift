---
title: Boundary operators
description: Contract for one-sided physical exterior-boundary conditions in the continuous-Galerkin solver.
---

# Boundary operators

A boundary operator enforces physics on the fixed exterior boundary of the computational domain. It is one-sided: one phase trace is paired with prescribed data or an exterior model. It is intentionally separate from a two-sided [material-interface operator](14-interface-operators.md), which couples independently evolving phase states across a moving level-set surface.

This separation prevents a boundary id from selecting phase-change kinetics and prevents an interface law from acquiring wall-specific assumptions. The [geometry and topology service](03-geometry-topology.md) classifies physical exterior fragments, while [typed worksets](05-worksets.md) deliver fragments already grouped by phase, boundary id, operator kind, and `GeometrySnapshotId`.

The initial discretization is continuous Galerkin. Boundary operators therefore provide the consistent natural, characteristic, and Nitsche terms required by the CG weak form. The contract leaves room for a later DG numerical-boundary-flux implementation without pretending the stabilization mechanisms are interchangeable.

## Responsibilities

A boundary operator:

- consumes a one-sided phase boundary trace and the outward domain normal;
- evaluates time- and position-dependent prescribed data;
- returns conservative advective, diffusive, traction, heat, and species contributions in the phase's canonical residual convention;
- supplies adjoint-consistent Nitsche consistency, symmetry, and penalty terms when weak essential conditions are selected;
- declares which variables are prescribed, natural, characteristic, or constrained;
- validates inflow/outflow compatibility and the number of incoming characteristics;
- exposes exact Jacobian action and a separately identified preconditioner approximation;
- contributes boundary mass, momentum, energy, species, and entropy diagnostics.

The operator selected for a fragment may depend on the phase PDE family. Fully compressible inflow and outflow require characteristic information. Low-Mach and incompressible walls participate in velocity/pressure compatibility and may contribute to a connected-region volume-flux integral. A solid boundary supplies traction, velocity, and thermal conditions using the same traction-work convention as its energy residual.

## Non-responsibilities

A boundary operator does not:

- discover boundary faces or inspect neighboring cells to classify them;
- choose which phase occupies a boundary fragment;
- solve interface temperature or phase-change rates;
- exchange a flux between two phase residuals;
- evolve a contact line or impose a wetting/contact-angle law;
- own the time integrator, pressure gauge, or global nonlinear solve;
- initialize newly activated phase support.

Contact lines are outside the initial model. Geometry validation must fail if the moving material interface intersects the exterior boundary in a configuration requiring a contact angle, wall adsorption, moving-contact-line slip, or line energy. A later contact-line module will be a codimension-two operator, not an option on this boundary interface.

## Conceptual API

```cpp
template<int dim, class Number>
struct BoundaryTrace {
  PhaseId phase;
  PhysicsKind physics;
  Point<dim, Number> position;
  Tensor<1, dim, Number> outward_normal;
  ThermodynamicTrace<Number> thermo;
  KinematicTrace<dim, Number> kinematics;
  SpeciesTraceView<Number> species;
  DiffusiveTraceView<dim, Number> diffusive;
};

template<int dim, class Number>
struct BoundaryContribution {
  PhaseResidualFlux<dim, Number> conservative_flux;
  NitscheTerms<dim, Number> nitsche;
  BoundaryDiagnostics<Number> diagnostics;
};

template<class Policy>
class CgBoundaryKernel {
public:
  template<int dim, class Number>
  BoundaryContribution<dim, Number> evaluate(
      const BoundaryTrace<dim, Number> &trace,
      const BoundaryPointContext<dim, Number> &context,
      const PolicyData &data) const;
};
```

At runtime, `(BoundaryId, PhaseId)` resolves to a boundary specification. The phase's compiled-registry visitor then dispatches once per homogeneous boundary workset to a static `CgBoundaryKernel<Policy>`. Prescribed functions and tabulated data are runtime objects evaluated into batch data before or within the kernel through allocation-free views.

The returned flux uses the outward physical-domain normal. No caller flips signs ad hoc. For a conservation law, the operator reports the physical flux leaving the phase; assembly applies the weak-form sign exactly once.

## Supported operator families

The first architecture should accommodate:

- impermeable no-slip or slip walls, imposed strongly only when the field space and conservation contract allow it, otherwise by Nitsche;
- prescribed traction and moving-wall work;
- adiabatic, prescribed-temperature, prescribed-heat-flux, and finite thermal-resistance boundaries;
- noncatalytic zero species flux, prescribed composition, and specified species flux;
- fully compressible characteristic inflow, outflow, and far-field data;
- low-Mach open boundaries with prescribed thermodynamic pressure where configured;
- incompressible velocity/traction/open conditions with a declared pressure gauge policy;
- Eulerian-solid displacement/reference-map-compatible velocity, traction, and thermal data.

A boundary condition is a coherent policy, not an arbitrary per-component list. For example, imposing total energy, temperature, and heat flux simultaneously is overspecified. Prescribing all species mass fractions must respect their sum constraint. A low-Mach closed region must provide boundary volume flux compatible with its global pressure equation.

## CG weak-enforcement contract

Nitsche terms use the same physical stress, heat flux, cross-diffusion operator, and test-space scaling as the phase residual. Penalties scale with polynomial degree, local geometry, material coefficients, and cut configuration where applicable. Pressure stabilization is not derived from the velocity penalty. Penalty values and cut-aware weights may be lagged in a preconditioner, but the exact residual uses the configured boundary law.

Momentum traction and energy traction work must be evaluated from the same numerical traction. Species enthalpy carried by boundary diffusion appears exactly once in the total nonadvective energy flux. Strong constraints, weak fluxes, and source terms may not double impose the same condition.

## Invariants and failure behavior

- Constant compatible states have zero boundary residual.
- Prescribed data have explicit units, frame, and thermodynamic convention.
- The number of hyperbolic conditions equals the number of incoming characteristics.
- Species data use the phase-local compact map resolved from global `SpeciesId`s.
- Closed-wall mass flux is exactly zero, and adiabatic/noncatalytic limits recover zero heat/species flux.
- Boundary power in momentum and total energy agrees to quadrature tolerance.
- An invalid state, overspecified condition, missing species, incompatible low-Mach volume flux, unsupported wall/interface intersection, or failed exterior-state solve returns a structured failure and rejects the stage or configuration.

## Required tests

1. Constant-state tests for every boundary family and normal orientation.
2. Manufactured convergence for Dirichlet, Neumann, Robin, traction, heat, and species data.
3. Characteristic acoustic reflection/transmission and subsonic inflow/outflow counting.
4. Closed-wall global mass and energy balance, including moving-wall work.
5. Coupled species/enthalpy flux accounting with unequal species diffusivities.
6. Low-Mach closed-volume and prescribed-pressure boundary compatibility.
7. Solid traction/thermal expansion tests using identical momentum and energy traction.
8. Scalar, SIMD, AD, assembled, and matrix-free consistency on the same boundary workset.
9. Geometry validation tests that reject contact-line configurations rather than silently applying a wall law.

Boundary traces are exported by the [PhaseSystem facade](10-phase-systems.md), and their local constitutive calculations follow [Model policies](11-model-policies.md).
