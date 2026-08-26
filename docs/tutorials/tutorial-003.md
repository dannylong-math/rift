---
title: "Tutorial 3: Publish a discrete-state update"
description: Create zero state, edit owner-local vectors collectively, seal a private candidate, and publish immutable accepted state.
---

# Tutorial 3: Publish a discrete-state update

The phase graph says what the phases mean, and the finalized space says what
each vector entry means. A `StateStore` supplies changing values for exactly
that layout. This tutorial creates the initial zero state, edits a private
copy, and publishes the result.

The complete program is [`tutorial-003.cpp`](https://github.com/dannylong-math/rift/blob/main/tutorials/tutorial-003.cpp).
Rift currently stops at this published discrete state; this example does not
pretend that a PDE operator or time integrator has already been implemented.

## Build the same layout

The first part combines Tutorials 1 and 2. It also adds one regional scalar,
`mean_pressure`, while finalizing the layout. Regional values are replicated
small pieces of state rather than finite-element fields.

<!-- rift:snippet tutorial-003.build-layout -->

## Create accepted zero state

The state factory allocates owner-partitioned deal.II vectors and creates an
initial accepted snapshot filled with zeros. A checked field reference carries
the complete space provenance, optional owning phase, and field-group ID.

<!-- rift:snippet tutorial-003.create-state -->

See [`StateStore`](/api/rift-StateStore/) and
[`StateFieldReference`](/api/rift-StateFieldReference/) for the full
collective and checked-local contracts.

## Edit a private trial

A mutable transaction starts from an immutable retained snapshot. Only locally
owned vector entries are edited here. Rift's current state-vector API does not
promise synchronized ghost values, so code that needs ghosts must establish a
separate higher-level contract.

<!-- rift:snippet tutorial-003.edit-trial -->

Every rank calls the regional update with the same entry and the same exact
floating-point representation. Other collective state operations must also be
entered in the same order on every rank.

## Seal, then publish

Sealing freezes the private vectors but does not make them accepted state.
Publishing is a separate collective operation. This separation lets nonlinear
or time-integration code inspect candidates without changing the live state.

<!-- rift:snippet tutorial-003.seal-and-publish -->

The [`MutableStateTransaction`](/api/rift-MutableStateTransaction/) reference
lists the recoverable transition errors and lifetime rules.

## Compare accepted and previous state

Successful publication keeps the candidate's snapshot identity, assigns an
accepted epoch, and moves the former accepted snapshot into the `previous`
slot. External copies remain immutable.

<!-- rift:snippet tutorial-003.inspect-snapshots -->

The executable checks the exact owner-local value of every new field entry,
the exact zero representation of initial and previous state, the regional
scalar bits, the accepted epoch, and the level-set revision. At this point an
application has a practical, versioned discrete state ready for a future solver
layer.
