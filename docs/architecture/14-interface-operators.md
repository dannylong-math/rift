---
title: Interface operators
description: Contract for conservative two-sided material-interface coupling and local finite-rate closure.
---

# Interface operators

An interface operator couples two phase systems across one oriented, moving material interface. It owns the two-sided physical jump law and its discrete CG coupling; neither adjacent [PhaseSystem facade](10-phase-systems.md) owns that relationship. The [runtime phase graph](01-phase-graph.md) selects the phase pair and interface-law data, while [geometry and topology](03-geometry-topology.md) supplies an oriented interface fragment and [typed worksets](05-worksets.md) batch fragments with compatible compiled implementations.

The canonical orientation is from side `minus` to side `plus`. All traces, transfer rates, relative fluxes, normals, and diagnostics use that orientation. Reversing an edge in the phase graph must produce an equivalent exchange through an explicit orientation transform, never through scattered sign fixes inside phase kernels.

## Canonical traces

Each phase converts its private state layout into the same semantic trace:

```cpp
template<int dim, class Number>
struct InterfaceTrace {
  PhaseId phase;
  PhysicsKind physics;
  ThermodynamicTrace<Number> thermo;
  KinematicTrace<dim, Number> kinematics;
  StressTrace<dim, Number> stress;
  EnergyTrace<dim, Number> energy;
  SpeciesTraceView<Number> species;
  DiffusiveTraceView<dim, Number> diffusive;
  ConstraintTrace<Number> constraints;
};
```

The trace exposes physical meanings such as density, velocity, temperature, pressure/traction, energy or enthalpy, chemical potentials, one-sided species fluxes, and heat flux. It does not expose conserved-vector offsets. A low-Mach trace can therefore provide `p₀ + π` traction information without pretending its state is compressible total energy; a solid trace provides Cauchy stress without pretending it has fluid pressure and viscosity.

Species arrays use each phase's compact local ordering plus a read-only `PhaseSpeciesMap`. Interface channel data use globally stable `SpeciesId`s and are resolved into the two local maps during finalization. No union-sized bulk species vector is created.

## Local closure and exchange

The initial surface has no globally stored mass, species, or energy. At every interface quadrature point, the operator solves a small algebraic closure for

```text
yΓ = (TΓ, {ξ̇rΓ}, q′−→Γ, q′+→Γ, VΓ).
```

Depending on the chosen algebraic formulation, heat fluxes or `VΓ` may be eliminated, but the returned exchange has the same canonical form:

```cpp
template<int dim, class Number>
struct InterfaceSideExchange {
  SparsePhaseSpeciesFluxView<Number> species_fluxes;
  Tensor<1, dim, Number> momentum_flux;
  Number total_energy_flux;
};

template<int dim, class Number>
struct InterfaceExchange {
  Number interface_temperature;
  Number normal_speed;
  Number common_mass_flux;
  InterfaceSideExchange<dim, Number> minus;
  InterfaceSideExchange<dim, Number> plus;
  SurfaceMomentumTerm<dim, Number> capillary_momentum;
  SurfaceEnergyTerm<Number> surface_work;
  OptionalInterfaceActivationData<dim, Number> activation;
  InterfaceDiagnostics<Number> diagnostics;
};
```

For either side, the moving-interface relative flux uses the one canonical
normal:

```text
gU,α = Fα(Uα, ∇Uα) · n − VΓ Uα,  α ∈ {−, +}.
```

The closure evaluates both side bundles together and scatters each exactly once
into its phase residual. Common conserved transfer uses opposite outward
orientation, but individual species entries are phase-qualified and need not
be negatives of one another when an interface reaction changes identity. The
two sparse species bundles obey the channel stoichiometry and recover the same
common total mass flux. A channel may describe identity-preserving transfer or
identity-changing transfer such as one condensed polymer producing several
gas species.

The closure enforces species conditions, common total mass flux, finite-rate normal heat transfer, kinetic rate laws, the non-capillary momentum jump, and total-energy balance. The normal speed satisfies

```text
VΓ = vα · n − ṁ / ρα.
```

Consequently, phase change generally permits a normal-velocity jump. Temperature also may jump. The clean-interface baseline weakly enforces tangential velocity continuity; it must not apply a generic full-vector or temperature-continuity penalty.

## Static condensation and differentiation

The local nonlinear solve returns its factorization and implicit-function tangent. For local equations `gΓ(yΓ, z−, z+, G) = 0`,

```text
∂yΓ/∂z = −(gΓ,y)⁻¹ gΓ,z.
```

Here `G` includes the current quadrature position, normal, projector, and surface derivatives. The exact Jacobian action differentiates the same closure as the residual. Small dense factorizations may be cached only for the current `GeometrySnapshotId` and `StateSnapshotId`. The preconditioner may lag selected thermodynamic or geometry derivatives, but must label that approximation.

At runtime the phase pair and law identifier select a compiled

```cpp
InterfaceKernel<MinusPhaseTraits,
                PlusPhaseTraits,
                CgTraceCoupling,
                TransferLaw,
                SurfaceTensionLaw>;
```

Dispatch occurs once per homogeneous interface workset. Local quadrature loops are scalar-generic for `double`, `float`, SIMD, and AD according to [Model policies](11-model-policies.md).

## Surface terms and CG coupling

The initial surface-tension law is linear in algebraic interface temperature,

```text
σ(TΓ) = a₀ + a₁ TΓ.
```

It has no independent surface-temperature evolution equation, yet its Marangoni traction and surface-area work remain. Capillarity is evaluated in surface-divergence form from the same quadrature, normal, projector, `TΓ`, and surface derivative used by the energy-work term. The capillary contribution is represented in `InterfaceExchange` but is not inserted again into the common bulk flux.

For the CG-first discretization, each phase retains an independent trace on a cut cell. Coefficient- and cut-aware Nitsche terms impose only the actual tangential, viscous, and thermal-resistance laws. The single interface closure and exchange are the conservation authority. A later DG adapter may turn the same physical exchange into a material Riemann flux, but that is not part of the initial implementation.

## Responsibilities and non-responsibilities

The operator owns pair-specific traces, transfer channels, local closure, relative flux, Nitsche coupling, capillary/surface-work pairing, local tangent, and interface diagnostics. It does not own level-set transport, active-mesh classification, stage acceptance, exterior boundary conditions, global phase fields, or connected-region pressure unknowns.

There is no global surface state initially: no adsorption, surface species, surface heat capacity, tangential surface conduction, or surface diffusion. Adding any of these requires a new surface-state system and surface worksets rather than enlarging the algebraic closure invisibly. Contact lines are also excluded; an interface fragment meeting the exterior boundary is rejected unless a future codimension-two law is configured.

## Invariants and failure behavior

- One closure solve and one common oriented flux are used per quadrature point.
- Reversing orientation swaps sides and signs without changing physical totals.
- Every channel conserves total mass and chemical elements.
- Summed phase species fluxes recover the same common mass flux.
- Momentum traction and energy traction work use the same numerical traction.
- Bulk-plus-surface momentum and energy balances hold when phase residuals are summed.
- Entropy production is nonnegative for the configured heat and transfer laws.
- Invalid traces, unresolved species, nonconservative stoichiometry, negative interface temperature, inadmissible one-way rate, singular closure Jacobian, or inconsistent side values of `VΓ` return a structured local failure. The global stage is damped or rejected; the operator never clips a rate or silently assumes equilibrium.

## Required tests

1. Orientation-reversal and opposite-scatter tests.
2. Zero-transfer stationary-interface and equal-state limits.
3. Unequal species sets with identity-preserving and identity-changing manufactured channels.
4. Mass, elemental, momentum, total-energy, surface-work, and entropy ledgers.
5. Temperature-jump and finite thermal-resistance limits.
6. Normal-velocity jump and interface-speed consistency from both sides.
7. Laplace pressure, thermocapillary traction, and capillary-work tests using one surface quadrature.
8. Local condensed tangent versus AD and finite differences.
9. Scalar/SIMD and assembled/matrix-free agreement on identical interface worksets.
10. Explicit rejection tests for surface-state and contact-line configurations outside the initial model.

Exterior walls, inlets, and outlets remain the responsibility of [Boundary operators](13-boundary-operators.md).
