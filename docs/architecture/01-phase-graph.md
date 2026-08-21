---
title: Runtime phase graph
description: Contract for named phases, pairwise material interfaces, and future junction operators.
---

# Runtime phase graph

## Context

Rift must run problems with different numbers and kinds of phases without
recompiling one solver for every phase arrangement. At the same time, a phase's
physics must remain statically specialized enough for efficient cell kernels.
The runtime phase graph is the boundary between those requirements.

A graph node is a **named phase instance**. Its physics type is selected when
the run is constructed and is fixed for the run. Its geometrical occupancy is
not fixed: the set of cells and cut-cell fragments occupied by that phase may
change as the full-background level-set field set evolves. A graph edge is an allowed
pairwise material interface and owns the identity of the corresponding
interface operator. Geometry decides whether and where an edge is currently
realized.

## Responsibilities

The phase graph must:

- assign stable identifiers to every named phase and declared interface;
- retain the fixed physics type and model configuration of each phase;
- describe an arbitrary runtime collection of phase nodes and pairwise edges;
- give every edge a stable minus/plus orientation, independent of cell
  traversal order;
- validate that an interface operator is compatible with the physics and
  species sets on both incident phases;
- provide lookup by stable identifier without using names in hot loops; and
- reserve a separate extension point for codimension-two and
  codimension-three junction laws.

The graph describes *permitted interactions*. Local geometrical pieces of an
already realized edge may appear or disappear as an interface crosses cells;
this changes cut-cell routing, not physical topology or phase identity. The
representation can describe an entire edge becoming realized later, but the
initial solver treats that adjacency change as a deferred, terminal topology
transition even when the edge was declared in advance.

## Non-responsibilities

The graph does not classify cells, reconstruct interfaces, generate cut
quadrature, own solution vectors, distribute degrees of freedom, or schedule
assembly. Those jobs belong respectively to [geometry and
topology](03-geometry-topology.md), [discrete state](02-discrete-state.md), and
[typed workset routing](05-worksets.md).

The initial graph also does not reduce a junction law to a sequence of
pairwise fluxes. A point or curve where three or more phases meet generally has
its own force balance, kinematics, and possibly constitutive state. Treating it
as an arbitrary ordering of pairwise operators would make the result depend on
assembly order.

## Conceptual C++ API

The following names illustrate ownership rather than prescribe final spelling:

```cpp
using PhaseId = StrongId<struct PhaseTag>;
using InterfaceId = StrongId<struct InterfaceTag>;

struct PhaseDescriptor {
  PhaseId id;
  std::string name;
  PhysicsKey physics_key;
  PhaseSystemHandle system;  // Bound once by a runtime factory.
};

struct InterfaceDescriptor {
  InterfaceId id;
  PhaseId minus_phase;
  PhaseId plus_phase;
  InterfaceOperatorHandle operator_handle;
};

class PhaseGraph {
public:
  std::span<const PhaseDescriptor> phases() const;
  std::span<const InterfaceDescriptor> interfaces() const;
  const PhaseDescriptor &phase(PhaseId) const;
  const InterfaceDescriptor &interface(InterfaceId) const;
  void validate() const;
};
```

`PhysicsKey` is a runtime construction key, not a quadrature-point dispatch.
The factory behind `PhaseSystemHandle` binds one compiled phase-system type to
the node before assembly begins. Changing cell occupancy therefore changes the
worksets passed to that system, not the system's dynamic type.

A future API adds a `JunctionDescriptor` and `JunctionOperator`. It consumes a
dedicated junction workset containing all incident phases and orientations.
The pairwise `InterfaceOperator` contract remains unchanged.

## Invariants and lifetime

Phase and interface identifiers are unique and never recycled during a run.
Names are unique configuration labels; persisted state and worksets use the
strong identifiers. An interface may not join a phase to itself, and its
minus/plus order is stable for its lifetime.

The graph's configured nodes, their physics bindings, and its allowed
pairwise edges are immutable during a nonlinear solve. Realized local cut
pieces and interface measure belong to a geometry snapshot and may change
while physical phase adjacency and component topology remain fixed. Replacing
a graph or changing a phase's physics is a
new run configuration, not a state update.

Objects derived from the graph carry the relevant epoch stamps described in
[worksets](05-worksets.md). A graph identifier remains meaningful across epochs,
but a cell list or local degree-of-freedom view associated with it does not.

## Failure behavior

Graph construction fails before degrees of freedom are distributed when it
finds duplicate identities, missing incident phases, self-edges, incompatible
state schemas, or an unavailable interface-operator combination. It must
report phase and interface names in diagnostics even though runtime kernels use
numeric identifiers.

If geometry detects a three-or-more-phase junction before a compatible
`JunctionOperator` exists, the solve stops with an unsupported-junction
diagnostic. Rift must not silently omit the junction or choose an arbitrary
pairwise ordering.

## Contract tests

The graph test suite must cover:

1. construction of graphs with one, two, and more than two named phases;
2. stable lookup and edge orientation independent of insertion order;
3. rejection of duplicate names, self-edges, and missing incident phases;
4. rejection of incompatible phase/interface model combinations;
5. unchanged phase-system identity while occupied cell sets move;
6. activation and disappearance of local geometrical pieces without graph or
   regional-layout mutation;
7. deterministic serialization of the configured graph; and
8. explicit failure when a detected junction lacks a `JunctionOperator`.

The [discrete-state contract](02-discrete-state.md) defines how every graph node
receives phase-local fields. The [geometry contract](03-geometry-topology.md)
turns graph identities into current phase regions, and the [workset
contract](05-worksets.md) carries those identities into numerical operators.
