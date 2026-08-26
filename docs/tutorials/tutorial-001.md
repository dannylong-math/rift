---
title: "Tutorial 1: Describe a multiphase run"
description: Create an MPI run, name its phases, orient an interface, and carry checked phase references into later setup.
---

# Tutorial 1: Describe a multiphase run

Every Rift calculation starts by describing the parts of the physical problem
that do not change while the solution advances. In this first tutorial, those
parts are two phases and the material interface between them. We will call the
phases `liquid` and `gas` and orient the interface from liquid to gas.

The complete program is [`tutorial-001.cpp`](https://github.com/dannylong-math/rift/blob/main/tutorials/tutorial-001.cpp).
It runs with two MPI ranks in the normal Rift test suite.

## Give Rift an MPI communicator

Rift's setup functions are collective: every rank calls them in the same
order with the same logical input. A `RunConfiguration` gives the calculation
a shared identity and retains its own duplicate of `MPI_COMM_WORLD`. The MPI
RAII object in `main()` is created before this code, so the run and everything
derived from it are destroyed before MPI is finalized.

<!-- rift:snippet tutorial-001.run-configuration -->

The result is an `std::expected`. Real applications should inspect it rather
than assuming setup succeeded. See the generated
[`RunConfiguration` reference](/api/rift-RunConfiguration/) for the complete
error list and communicator rules.

## Name phases and orient their interface

A phase specification contains a readable name and a key for the phase
physics that an application has compiled. An interface similarly has a name,
an explicit minus phase, an explicit plus phase, and an operator key. Rift
stores these keys but does not yet provide the physics or operator registry
itself. The callback below stands in for the application's registry check.

<!-- rift:snippet tutorial-001.phase-and-interface-input -->

The order `liquid`, then `gas`, is meaningful. Rift preserves it as the
minus/plus orientation; it is not inferred from alphabetical order or numeric
identifiers. The callback must be deterministic because every MPI rank checks
the same pairing.

## Build the immutable graph

Graph construction validates all names, resolves the interface endpoints, and
gives each object a stable identifier. It may report several configuration
errors at once, which lets an input file be repaired in one pass.

<!-- rift:snippet tutorial-001.build-graph -->

The resulting [`PhaseGraph`](/api/rift-PhaseGraph/) is immutable. Geometry may
move later, but the identity of the configured liquid, gas, and interface does
not change.

## Carry checked identities forward

Names are convenient during setup. Later components should carry
`PhaseReference` values instead. A reference combines the small phase number
with the run and graph provenance that gives that number meaning.

<!-- rift:snippet tutorial-001.inspect-graph -->

The checks at the end are also the executable smoke test. The program returns
success only if both ranks see the requested two-phase topology and each rank's
compatibility callback was invoked exactly once with the oriented liquid-to-gas
finite-rate interface. Tutorial 2 will use these phase references to build
finite-element spaces.
