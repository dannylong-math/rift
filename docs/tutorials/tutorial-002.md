---
title: "Tutorial 2: Build distributed finite-element spaces"
description: Put a deal.II mesh under Rift ownership and build phase-local and full-background spaces on two MPI ranks.
---

# Tutorial 2: Build distributed finite-element spaces

Tutorial 1 described which phases exist. We now describe where their unknowns
live. The example builds a small distributed two-dimensional mesh, assigns
each owned cell to one phase for teaching purposes, and asks Rift to create
finite-element spaces.

The complete program is [`tutorial-002.cpp`](https://github.com/dannylong-math/rift/blob/main/tutorials/tutorial-002.cpp).

## Recreate the run description

Each tutorial is a standalone program. We therefore begin with the same MPI
run and phase graph used in Tutorial 1.

<!-- rift:snippet tutorial-002.describe-run -->

## Create a distributed deal.II mesh

A deal.II triangulation stores cells, refinement relationships, and which MPI
rank owns each active cell. `subdivided_hyper_cube(..., 2)` makes four square
cells in the unit square. `MeshSnapshot` then takes ownership of the completed
triangulation and exposes it only through a const view.

<!-- rift:snippet tutorial-002.distributed-mesh -->

Rift reconstructs a distributed triangulation on a communicator it owns. We
therefore inspect cell identities from the snapshot, not from the consumed
input pointer. The generated [`MeshSnapshot` reference](/api/rift-MeshSnapshot/)
describes its ownership and communicator guarantees.

## Request owner-local phase support

Every represented phase supplies exactly one support record on every rank.
That record contains only active cells owned by that rank; an empty local set
is valid. Here, cell centers left of `x = 0.5` request liquid support and the
remaining cells request gas support.

<!-- rift:snippet tutorial-002.owner-local-support -->

This center test is only a clear teaching policy. Rift does not currently
provide a physical geometry classifier, and a production application would
derive these sets from its own interface geometry. The executable checks the
center rule independently for every locally owned cell, so exchanging the two
phase assignments cannot still satisfy its smoke test.

We then pair each owner-local set with the checked phase reference and the mesh
snapshot where its cell identities were observed.

<!-- rift:snippet tutorial-002.support-records -->

## Describe and build the spaces

Phase fields receive a real finite element only on the closed support of their
phase. The level-set field is different: it is defined on the complete
background mesh because it describes geometry everywhere. `begin_draft()`
builds these fields, and `finalize()` publishes an immutable layout suitable
for state vectors.

<!-- rift:snippet tutorial-002.build-spaces -->

See [`SpaceRegistry`](/api/rift-SpaceRegistry/) for validation details and
[`SpaceSnapshot`](/api/rift-SpaceSnapshot/) for the immutable result.

## Inspect the result

Field declaration order is not an identity contract. We look fields up by
phase reference and name, then retain the returned identifiers for later use.
The executable also confirms that every requested owner cell remains in the
final closed phase support and that no regional scalar was requested.

<!-- rift:snippet tutorial-002.inspect-spaces -->

The layout has three vector blocks: gas state, liquid state, and the
full-background level set. Tutorial 3 will allocate values for this layout and
publish an update.
