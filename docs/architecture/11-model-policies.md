---
title: Model policies
description: Static policy contracts for thermodynamics, transport, reactions, constitutive laws, and scalar-generic kernels.
---

# Model policies

Model policies implement local, model-dependent calculations without owning finite-element traversal or global solver state. They are composed into a concrete phase kernel behind the [PhaseSystem facade](10-phase-systems.md). Runtime selection happens through the compiled registry; once a workset is dispatched, calls among policies are statically resolved and inlineable.

The central separation is between **policy type** and **policy data**. A policy type chooses an algorithm and its differentiable contract. Runtime data supplies species, coefficients, tables, and reaction channels. This avoids recompiling for every mechanism while keeping virtual calls, allocation, string lookup, and configuration branching out of hot loops.

## Policy bundle

A fluid bundle normally contains thermodynamics, viscous stress,
multicomponent transport, thermal transport, bulk chemistry, body/source
terms, state recovery, and admissibility policies. A solid bundle replaces
fluid transport and chemistry as appropriate with stored energy, constitutive
history, return mapping, and thermal coupling. [Stabilization and conservative
limiting](12-stabilization-and-admissibility.md) are discretization policies and
remain distinct from physical diffusion.

```cpp
template<class Thermodynamics,
         class Stress,
         class SpeciesTransport,
         class HeatTransport,
         class Reactions,
         class Sources,
         class Admissibility>
struct ModelBundle;

template<class Bundle, class Number>
concept PhaseModel = requires(Bundle model,
                              LocalStateView<Number> state,
                              LocalGradientView<Number> gradient) {
  { model.thermodynamics.evaluate(state) };
  { model.transport.flux(state, gradient) };
  { model.reactions.source(state) };
  { model.admissibility.check(state) };
};
```

The concepts should check return shapes and capabilities rather than require inheritance. Policies return value objects plus explicit status information; throwing from a SIMD quadrature loop is not the normal failure path.

## Scalar-generic contract

Every differentiable hot policy is templated on `Number`. Supported number families are scalar `double` and `float`, deal.II-compatible SIMD packs, and the selected automatic-differentiation type. A policy must not unpack a SIMD lane or erase an AD derivative simply to call a scalar library.

```cpp
struct HelmholtzPolicy {
  template<class Number>
  ThermoState<Number> evaluate(
      Number temperature,
      std::span<const Number> partial_densities,
      const ThermoData &data) const;

  template<class Number>
  RecoveryResult<Number> recover_temperature(
      const ConservativeStateView<Number> &state,
      const ThermoData &data) const;
};
```

Branches must be based on a documented primal-value decision and produce the same branch for residual and derivative evaluation. Nonsmooth guards, table-region changes, limiter activation, and reaction active sets must report that the current tangent is only semismooth or invalid. Finite differences are a verification tool, not the default way to differentiate unrelated EOS calls in different blocks.

## Species identity and compact phase layouts

The application owns one immutable `SpeciesCatalog`. Each `SpeciesId` is globally stable and records canonical identity, molar mass, elemental composition, and optional aliases. A phase owns a `PhaseSpeciesMap`:

```cpp
struct PhaseSpeciesMap {
  std::span<const SpeciesId> local_to_global() const;
  std::optional<LocalSpeciesIndex> find(SpeciesId) const;
};
```

Bulk arrays use compact local indices `[0, N_phase)`. They never allocate the union of all application species. Interface channels and configuration files use global `SpeciesId`s, then resolve them once into phase-local indices during finalization. An unresolved reactant is an error; a species absent from one side is valid only when the channel stoichiometry does not require a nonexistent one-sided trace.

Species and reaction data are runtime data. A reaction mechanism contains phase-local stoichiometric sparse rows, rate-family tags, parameters, and elemental-conservation metadata. The registry maps each supported rate-family tag to a compiled evaluator, then groups reactions by evaluator type so the inner loop is static over a contiguous group. Per-reaction virtual dispatch is forbidden.

## Thermodynamic consistency

For a fluid phase, pressure, chemical potentials, entropy, internal energy, enthalpy, heat capacities, and required derivatives come from one Helmholtz or equivalent thermodynamic model. Independently fitted properties must be reconciled before they enter a policy. Formation energies belong to this model, so reaction sources change total energy through composition and do not receive a second heat-of-reaction term.

State recovery returns both the recovered state and the exact derivatives needed by the Jacobian action. A failed bracket, unstable EOS branch, nonpositive heat capacity, or invalid compressibility is a local failure propagated to nonlinear globalization. A policy must not choose another phase branch silently.

Stefan–Maxwell transport operates on the constraint subspace with zero summed diffusive mass flux. The local policy may use a reduced basis or augmented saddle system, but deleting an arbitrary species equation and repairing the result afterward violates the contract. Soret and Dufour terms declare whether they are retained as a reciprocal pair or whether the configured model is explicitly approximate.

Reaction policies verify total-mass and elemental conservation when data are loaded. Reversible laws derived from activities or chemical potentials must use the same thermodynamics policy as transport and interface affinity calculations.

## Runtime registry boundary

A registry key includes PDE family, CG discretization tag, spatial dimension, scalar backend, and policy-family identifiers. Degree may be part of a compiled range rather than a unique registry entry. Configuration data are validated before vectors and DoFs are built.

```cpp
CompiledModelRegistry registry;
registry.add<CompressibleCg<MyEos, StefanMaxwell, Arrhenius>>(
    ModelKey{"compressible", "my-eos", "stefan-maxwell", "arrhenius"});

auto phase = registry.make(key, runtime_model_data);
```

Missing combinations fail at startup with a precise error. A fallback may change a preconditioner, but never the physical closure or exact residual.

## Responsibilities and non-responsibilities

Policies own local constitutive evaluation, local recovery, local source/flux evaluation, tangents, parameter validation, and local diagnostic quantities. They do not own `DoFHandler`s, workset iteration, phase topology, boundary ids, material-interface orientation, global pressure compatibility, time splitting, Newton tolerances, or cache lifetime.

The [interface operator](14-interface-operators.md) may reuse thermodynamic and rate policies through read-only views. A [boundary operator](13-boundary-operators.md) may reuse state recovery and characteristic data. Neither may mutate policy data during residual evaluation.

## Invariants, failures, and tests

- Global species ids remain stable across phases, restart, refinement, and repartitioning.
- Local species ordering is fixed for a finalized phase layout.
- Thermodynamic identities, units, and derivative conventions are common to residual, interface, and preconditioner construction.
- Local failures contain phase, workset, quadrature point, model revision, and reason.
- Runtime data revisions invalidate dependent EOS, chemistry, transport, and interface caches.

Tests include ideal and nonideal state sweeps; Maxwell/Gibbs–Duhem identities; scalar/SIMD lane equivalence; `double`/`float` tolerance studies; AD versus analytic and finite-difference tangents; Stefan–Maxwell nullspace and entropy production; reaction mass/element conservation; local temperature recovery; vanishing-species behavior; constitutive objectivity and consistent tangents; and registry rejection of malformed or unavailable configurations.
