---
title: Runtime phase graph
description: Implemented contract for named phases, pairwise material interfaces, stable identities, and future junction operators.
---

# Runtime phase graph

## Context

Rift must run problems with different numbers and kinds of phases without
recompiling one solver for every phase arrangement. At the same time, a
phase's physics must remain statically specialized enough for efficient cell
kernels. The runtime phase graph is the boundary between those requirements.

A graph node is a **named phase instance**. Its physics type is selected when
the run is constructed and remains fixed for the run. Its geometrical
occupancy is not fixed: cells and cut-cell fragments occupied by that phase
may change as the full-background level-set field set evolves.

A graph edge is an allowed pairwise material interface. Each edge has an
explicit, stable orientation from a minus phase to a plus phase. Geometry
decides whether and where that edge is currently realized; cell traversal
never changes its orientation.

## Implemented first-stage boundary

The first implementation provides:

- strong `PhaseId` and `InterfaceId` value types;
- immutable phase and interface descriptors;
- canonical identifier assignment and name lookup;
- one edge at most for each unordered pair of named phases;
- structural validation with collected, structured diagnostics;
- a cold-path compatibility hook for the future compiled operator registry;
  and
- deterministic, output-only JSON describing the configured graph identity.

The graph currently stores `PhysicsKey` and `InterfaceOperatorKey` values. It
does not introduce placeholder executable handles before `PhaseSystem` and
`InterfaceOperator` exist. Those later registry layers will bind compiled
implementations using the keys while preserving this graph's identifiers and
orientation contract.

## Public construction API

The implemented API is declared in `rift/phase_graph.hpp`. Configuration uses
names because they produce useful diagnostics; a successfully constructed
graph resolves those names to compact numeric identifiers:

```cpp
const auto graph = rift::make_phase_graph(
    {{"gas", "compressible"}, {"liquid", "low-mach"}},
    {{"surface", "liquid", "gas", "finite-rate"}},
    compatibility_check);

if (!graph) {
  for (const auto &error : graph.error())
    report(error.message);
}
```

`make_phase_graph` returns
`std::expected<PhaseGraph, std::vector<PhaseGraphError>>`. It reports
independent configuration mistakes together rather than requiring repeated
edit-and-restart cycles. A compatibility check is optional only for a graph
with no interfaces.

The compatibility callback receives the resolved minus descriptor, resolved
plus descriptor, and interface specification. A compiled registry can
therefore validate the ordered physics pairing, state schemas, species maps,
and operator availability without placing virtual dispatch in an evaluation
loop.

## Stable identifiers and lookup

Phase names and interface names are unique configuration identities. The
constructor sorts each set lexicographically and assigns contiguous IDs from
zero. Consequently, the same logical graph receives the same IDs regardless
of declaration order.

`phase(PhaseId)` and `material_interface(InterfaceId)` are constant-time
indexed lookups. `find_phase(name)` and `find_interface(name)` are checked,
logarithmic construction and diagnostic lookups. Kernels and worksets use
numeric IDs rather than names.

The minus/plus order is copied from the interface specification. It is not
sorted with the names or inferred from numeric IDs. Looking up an edge by its
two incident `PhaseId`s is unordered, but the returned descriptor retains its
declared physical orientation.

## Canonical JSON

`canonical_json()` produces a compact deterministic record such as:

```json
{
  "schema": "rift.phase_graph",
  "version": 1,
  "phases": [
    {"id": 0, "name": "gas", "physics": "compressible"},
    {"id": 1, "name": "liquid", "physics": "low-mach"}
  ],
  "interfaces": [
    {
      "id": 0,
      "name": "surface",
      "minus_phase": 1,
      "plus_phase": 0,
      "operator": "finite-rate"
    }
  ]
}
```

This record identifies the graph used by a run for regression tests,
diagnostics, and future checkpoint metadata. It is not a second solver-input
format and currently has no parser. It contains no geometry, occupied cells,
model coefficients, state vectors, or executable handles. The schema name and
version prevent a future representation change from being mistaken for the
same format.

## Responsibilities

The phase graph must:

- assign stable identifiers to every named phase and declared interface;
- retain the runtime construction key for each fixed phase physics type and
  interface operator;
- describe an arbitrary runtime collection containing at least one phase;
- give every edge a stable minus/plus orientation;
- reject a second edge for the same unordered phase pair in the initial simple
  graph;
- invoke construction-time compatibility validation for every edge;
- provide lookup by stable identifier without names in hot loops; and
- reserve a separate extension point for codimension-two and
  codimension-three junction laws.

The graph describes *permitted interactions*. Local pieces of an already
realized edge may appear or disappear as an interface crosses cells. This
changes cut-cell routing, not graph topology or phase identity. Realizing or
removing an entire physical adjacency remains a topology event for the first
solver implementation.

## Non-responsibilities

The graph does not classify cells, reconstruct interfaces, generate cut
quadrature, own solution vectors, distribute degrees of freedom, bind
executable kernels, or schedule assembly. Those jobs belong to [geometry and
topology](03-geometry-topology.md), [discrete state](02-discrete-state.md),
[phase systems](10-phase-systems.md), [interface
operators](14-interface-operators.md), and [typed workset
routing](05-worksets.md).

The initial graph also does not reduce a junction law to a sequence of
pairwise fluxes. A point or curve where three or more phases meet generally
has its own force balance, kinematics, and possibly constitutive state.
Treating it as an arbitrary ordering of pairwise operators would make the
result depend on assembly order.

## Invariants and lifetime

Phase and interface identifiers are unique and never recycled during a graph's
lifetime. Names, physics keys, operator keys, incident phases, and minus/plus
orientation are immutable after successful construction.

The graph is immutable during a nonlinear solve. Realized cut pieces and
interface measure belong to a geometry snapshot and may change while the
configured graph remains fixed. Replacing a graph or changing a phase's
physics is a new run configuration, not a state update.

Objects derived from the graph carry the epoch stamps described in
[worksets](05-worksets.md). A graph identifier remains meaningful across
epochs, but a cell list or local degree-of-freedom view associated with it does
not.

## Failure behavior

Construction fails before degrees of freedom are distributed when it finds:

- no phases, empty names, or empty registry keys;
- duplicate phase or interface names;
- missing incident phases or a self-interface;
- multiple edges for one unordered phase pair;
- an omitted compatibility check for an interface-bearing graph; or
- an incompatible phase/operator combination reported by the registry hook.

Diagnostics carry a structured error code and a human-readable message naming
the relevant phases and interfaces.

Junction detection belongs to geometry. When geometry later detects three or
more incident phases without a compatible `JunctionOperator`, the solve must
stop with an unsupported-junction diagnostic. It must not omit the junction or
choose a pairwise assembly order.

## Tests

The current focused unit tests cover:

1. construction with one and more than two named phases;
2. canonical IDs, stable name lookup, and unordered incident-pair lookup;
3. edge orientation independent of phase declaration order;
4. rejection of an empty graph, duplicate names, self-edges, missing phases,
   and duplicate phase pairs;
5. required and rejecting compatibility callbacks;
6. collection of independent configuration errors; and
7. deterministic serialization independent of declaration order.

Three architecture tests require later components and remain explicit
integration obligations: unchanged phase-system binding while occupancy moves,
activation and disappearance of local geometry pieces without graph mutation,
and failure when geometry detects an unsupported junction.

The [discrete-state contract](02-discrete-state.md) defines how every graph node
receives phase-local fields. The [geometry contract](03-geometry-topology.md)
turns graph identities into current phase regions, and the [workset
contract](05-worksets.md) carries those identities into numerical operators.
