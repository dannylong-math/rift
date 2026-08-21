---
title: Phase activation
description: Conservative swept-region activation, fictitious-band initialization, and fluid-solid transformation state contracts.
---

# Phase activation

When a moving interface makes a previously fictitious fragment physical, Rift
must define more than a value to plot there. The new physical inventory must
come from the one same-phase swept-domain balance; when an actual phase-change
channel transfers material, that balance uses the same interface exchange as
the donor phase. History-bearing physics must additionally receive a valid
material state.

`PhaseActivationPolicy` is the phase- and interface-specific contract for this
transition. It is separate from mesh `SolutionTransfer`, level-set
reinitialization, and the numerical extension that keeps inactive-band degrees
of freedom well conditioned.

## Three distinct operations

Rift distinguishes:

1. **Fictitious extension**: while a cell lies inside a phase envelope but
   outside its physical region, extension or ghost-stabilization equations
   determine its algebraic unknowns. Those values carry no physical inventory.
2. **Swept physical activation**: during a time step, temporal geometry gives a
   fragment nonzero physical measure. The existing mass/flux residual consumes
   the policy's activation state and history; the policy is not another
   conserved source.
3. **Support allocation**: after an accepted-state envelope or mesh rebuild,
   newly allocated fictitious DoFs are initialized by the extension policy.
   Allocation alone does not create material.

Conflating these operations would double count mass/energy or mistake an
arbitrary smooth extension for the constitutive state of newly transformed
material.

## Interface capability

Each material-interface law declares whether either direction performs a
material transformation. When it does, its one local closure publishes
`InterfaceActivationData` together with `InterfaceExchange` and the normal
speed:

```cpp
struct InterfaceActivationData {
  PhaseId donor;
  PhaseId receiver;
  InterfaceId interface;
  InterfaceThermalState thermal;
  PhaseQualifiedComposition transfer_composition;
  KinematicActivationState kinematics;
  TransformationEnergyLedger energy;
  OptionalSolidTransformationState solid_state;
};
```

This record contains semantic physical quantities, not receiver-vector offsets.
The receiver phase's compiled activation policy maps it into the correct field
schema. An interface configuration is rejected at startup if a permitted
transformation direction lacks a compatible receiver policy.

## Conceptual API

```cpp
class PhaseActivationPolicy {
public:
  ActivationCapability capability() const;

  ActivationStateView prepare_swept_state(
      const PhaseActivationWorkset &,
      const EvaluationContext &,
      const TemporalGeometrySnapshot &,
      const OptionalInterfaceActivationDataView &,
      ConstPhaseStateView source,
      ActivationScratch &) const;

  void initialize_new_fictitious_support(
      const ExtensionContext &,
      MutablePhaseStateView candidate) const;

  ActivationAudit audit(const ActivationLedger &) const;
};
```

`PhaseActivationWorkset` contains swept fragments, old/new phase measures,
interface quadrature identity, receiver field views, and exactly-once MPI
ownership. It is derived from the same temporal-geometry snapshot used by the
phase mass term and relative interface flux.

## Fluid activation

For pure interface advection, a fluid receiver uses its own fictitious
extension state in the one same-phase space-time mass/flux term; there is no
donor transformation source. When a configured phase-change channel does
transform material, the policy obtains transferred species identities and
amounts from the phase-qualified interface exchange and provides a receiver
state consistent with its EOS and state family. A phase-change normal-velocity
jump is allowed; the policy must not force the receiver velocity to equal the
donor velocity.

Formation energy is already part of the thermodynamic model. Latent,
reaction, sensible, kinetic, traction-work, and surface-work contributions are
recorded in one transformation energy ledger and inserted once by the existing
interface/swept balance. The activation policy references that ledger but does
not add a second energy source. The receiver
cannot initialize absent species or copy the donor's compact species vector by
position.

## Solidification and melting

A solid receiver needs a reference/history state in addition to conserved
fields. Its policy defines the reference map or deformation measure at birth,
stress-free or prescribed transformation strain, material orientation,
temperature/enthalpy, and every constitutive history variable. The default
must be a declared stress-free transformation state in the chosen frame, not a
zero-filled history that can create an artificial elastic impulse.

For melting, stored elastic and transformation energy enters the same interface
energy ledger before the fluid state is constructed. Irreversible history that
has no liquid analogue is discarded only by a named constitutive transition
rule. These rules are model data and compiled policies, while interface
kinetics remain owned by the interface operator.

## Conservation and lifecycle

Swept activation supplies state/history to the exact stage residual. There is
no post-stage cell fill and no additional mass, momentum, species, or energy
source. The one mass/flux/activation-measure term must recover the phase,
species, elemental, momentum, and total-energy balance from [temporal geometry
and GCL](06-temporal-geometry-and-gcl.md). Transformation-specific history data
may add only the explicitly declared nonconserved constitutive equations.

The active envelope and all receiver DoFs remain fixed during the nonlinear
attempt. A trial activation uses private state, spatial geometry, and temporal
geometry snapshot ids. Rejection discards it. Acceptance publishes phase
history only with the complete time-step endpoint.

An envelope rebuild at an accepted state calls
`initialize_new_fictitious_support`; because those cells have zero receiver
physical measure, that operation is audited for algebraic extension quality,
not physical conservation. When they later become physical, the swept policy
still applies.

## Failure behavior and tests

Missing species mappings, an inadmissible receiver EOS state, a nonconservative
energy ledger, undefined solid history, inconsistent donor/receiver direction,
or a stale temporal snapshot rejects the trial. The policy never falls back to
copying a neighboring physical cell or zero-filling history.

Tests include one-dimensional Stefan motion; identity-changing evaporation;
unequal donor/receiver species sets; activation across cut cells, hanging
faces, and MPI partitions; liquid-to-solid birth with zero prescribed stress;
solid melting with stored-energy accounting; reversal of a reversible
transformation; scalar/SIMD and assembled/matrix-free agreement; exact donor
plus receiver ledgers; and rollback after every local failure. A support-only
test proves that initializing a new fictitious band changes no physical phase
integral.

Mesh-change transfer and its separate conservative correction are defined in
[adaptivity and transfer](08-adaptivity-and-transfer.md).
