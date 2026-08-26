---
title: Runtime phase graph
description: Accepted contract for run-scoped named phases, deterministic pairwise interface validation, and future junction operators.
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

## Verified baseline and required refinement

The existing baseline provides:

- strong `PhaseId` and `InterfaceId` value types;
- immutable phase and interface descriptors;
- canonical identifier assignment and name lookup;
- one edge at most for each unordered pair of named phases;
- structural validation with collected, structured diagnostics;
- a cold-path compatibility hook for the future compiled operator registry;
  and
- deterministic, output-only JSON describing the configured graph identity.

That baseline is verified for structurally valid serial construction, but it is
not the completed first-stage contract. Before this stage is accepted, the
implementation must also provide:

- explicit run-scoped graph provenance that is consistent on every rank in the
  run communicator;
- provenance-bearing phase references at boundaries where a bare numeric
  `PhaseId` could otherwise be mixed with an unrelated graph;
- nonassignable graph objects so assignment cannot invalidate borrowed
  descriptors while preserving the spelling of the same object;
- canonical compatibility-callback order and a deterministic, side-effect-free
  callback contract;
- a documented exception boundary for registry failures;
- UTF-8 validation for every name and registry key that enters canonical JSON;
  and
- permutation, callback, provenance, UTF-8, and invalid-identifier tests.

The graph currently stores `PhysicsKey` and `InterfaceOperatorKey` values. It
does not introduce placeholder executable handles before `PhaseSystem` and
`InterfaceOperator` exist. Those later registry layers will bind compiled
implementations using the keys while preserving this graph's identifiers and
orientation contract.

## Construction and run provenance

Configuration uses names because they produce useful diagnostics; a
successfully constructed graph resolves those names to compact numeric
identifiers. The approved construction boundary creates a run first and passes
that run directly to graph construction:

```cpp
const auto run = rift::RunConfiguration::create(MPI_COMM_WORLD);
if (!run) {
  report(run.error());
  return;
}

const auto graph = rift::make_phase_graph(
    *run,
    {{"gas", "compressible"}, {"liquid", "low-mach"}},
    {{"surface", "liquid", "gas", "finite-rate"}},
    compatibility_check);

if (!graph) {
  for (const auto &error : graph.error())
    report(error.message);
}
```

`RunConfiguration::create(MPI_Comm)` creates the run context, and
`make_phase_graph(const RunConfiguration &, ...)` receives it directly. Each
successful construction receives a never-reused, communicator-consistent
`PhaseGraphInstanceId`; together with the run's `RunConfigurationId`, it forms
the graph provenance. These semantics are fixed:

- the run identity is created collectively and has the same value on every
  participating rank;
- constructing identical graph content for two different runs does not make
  their phase or interface references interchangeable;
- copying a graph preserves both provenance fields, while a separate
  construction has a distinct `PhaseGraphInstanceId`; and
- a provenance-bearing phase reference combines the graph provenance and the
  graph-local `PhaseId` whenever an API must reject cross-graph mixing.

### Supported run communicators and identity allocation

`RunConfiguration::create()` supports live MPI intracommunicators wholly
derived from the current `MPI_COMM_WORLD`. This includes `MPI_COMM_WORLD`,
`MPI_COMM_SELF`, duplicates, splits and subgroups, and Cartesian communicators,
including communicators whose ranks have been reordered. Construction is
collective on the supplied communicator, and every participating rank must call
it in the same collective order.

A `RunConfigurationId` has the semantic components
`{origin_world_rank, per_origin_sequence}`. Rank zero in the supplied
communicator is translated to its rank in the current `MPI_COMM_WORLD`; that
origin process allocates a monotonically increasing, process-local sequence,
and the pair is broadcast on the supplied communicator. The pair is therefore
collision-free for overlapping, reordered, duplicate, and disjoint supported
intracommunicators without requiring a world-wide collective. It is never
reused during one MPI execution. No identity persistence or uniqueness across
separate executions or restarts is promised.

Intercommunicators are unsupported. An intracommunicator containing dynamic or
MPI Sessions processes that cannot all be translated into the same current
`MPI_COMM_WORLD` is also unsupported. These agreed logical limitations, as well
as finite identifier-space exhaustion, are reported through the factory's
expected error result. A non-success MPI return from communicator lifecycle,
query, group/rank translation, or collective operations is not a recoverable
configuration error: it is fatal to the participating run, because ranks may
no longer be able to rejoin a consistent collective state.

`make_phase_graph` returns
`std::expected<PhaseGraph, std::vector<PhaseGraphError>>`. It reports
independent configuration mistakes together rather than requiring repeated
edit-and-restart cycles. A compatibility check is optional only when the
caller supplied no interfaces. Supplying any interface specification requires
a callback even when structural errors prevent that interface from resolving;
the missing-callback diagnostic must not depend on the number of resolved
edges.

The compatibility callback receives the resolved minus descriptor, resolved
plus descriptor, and interface specification. A compiled registry can
therefore validate the ordered physics pairing, state schemas, species maps,
and operator availability without placing virtual dispatch in an evaluation
loop. The callback is a deterministic query: for the same arguments it returns
the same result and has no externally visible side effects. Rift invokes it
once for every structurally valid, unique edge in canonical interface-name
order. Returning a reason is an ordinary compatibility rejection. An exception
denotes a registry or program failure, propagates to the caller, and produces
no graph; it is not converted into a configuration incompatibility.

## Stable identifiers and lookup

Phase names and interface names are unique configuration identities. The
constructor sorts each set lexicographically and assigns contiguous IDs from
zero. Consequently, the same logical graph receives the same IDs regardless
of declaration order. These IDs are graph-local indices, not globally unique
tokens. Run and graph provenance makes cross-graph use detectable at component
boundaries.

`phase(PhaseId)` and `material_interface(InterfaceId)` are constant-time
indexed lookups. `find_phase(name)` and `find_interface(name)` are checked,
logarithmic construction and diagnostic lookups. Kernels and worksets use
numeric IDs rather than names.

The minus/plus order is copied from the interface specification. It is not
sorted with the names or inferred from numeric IDs. Looking up an edge by its
two incident `PhaseId`s is unordered, but the returned descriptor retains its
declared physical orientation.

The initial simple graph selects one complete interface law for each unordered
pair of named phases. `InterfaceOperatorKey` therefore identifies the complete
composite coupling law for that pair, including every enabled physical effect;
it is not one independently schedulable contribution. A registry may build
that composite law internally. Representing several independently ordered
edges for one phase pair would require a future multigraph contract and is not
part of this stage.

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

This record identifies canonical graph content for regression tests,
diagnostics, and future checkpoint metadata. It deliberately omits
`RunConfigurationId` and `PhaseGraphInstanceId`: graph provenance is paired
with the content record by the owning run metadata rather than making
otherwise identical configurations serialize differently. The JSON is not a
second solver-input format and currently has no parser. It contains no
geometry, occupied cells, model coefficients, state vectors, or executable
handles. The schema name and version prevent a future representation change
from being mistaken for the same format.

All phase names, interface names, incident-phase references, physics keys, and
operator keys must be well-formed RFC 3629 UTF-8 before graph construction can
succeed. Canonicalization preserves their exact UTF-8 byte spelling and uses
bytewise lexicographic order; it performs no Unicode normalization, case
folding, or locale-dependent comparison. JSON control characters are escaped.
Invalid UTF-8 is a structured configuration error rather than output that only
looks like JSON.

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
- stamp identities exported to other run components with graph provenance;
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

Phase and interface identifiers are unique and never recycled within one
graph provenance. Names, physics keys, operator keys, incident phases, and
minus/plus orientation are immutable after successful construction.

`PhaseGraph` is copy- and move-constructible as required for result transport,
but copy and move assignment are deleted. Copy construction preserves graph
provenance. References, spans, and string views borrow graph-owned storage and
remain valid only while the graph object from which they were obtained remains
alive.

The graph is immutable during a nonlinear solve. Realized cut pieces and
interface measure belong to a geometry snapshot and may change while the
configured graph remains fixed. Replacing a graph or changing a phase's
physics is a new run configuration, not a state update.

Objects derived from the graph carry its run/graph provenance in addition to
the epoch stamps described in [worksets](05-worksets.md). A graph-local
identifier remains meaningful across epochs of that graph, but a cell list or
local degree-of-freedom view associated with it does not.

## Failure behavior

Construction fails before degrees of freedom are distributed when it finds:

- no phases, empty names, or empty registry keys;
- malformed UTF-8 in any name, incident reference, or registry key;
- duplicate phase or interface names;
- missing incident phases or a self-interface;
- multiple edges for one unordered phase pair;
- an omitted compatibility check when any interface specification was
  supplied; or
- an incompatible phase/operator combination reported by the registry hook.

Diagnostics carry a structured error code and a human-readable message naming
the relevant phases and interfaces.

Junction detection belongs to geometry. When geometry later detects three or
more incident phases without a compatible `JunctionOperator`, the solve must
stop with an unsupported-junction diagnostic. It must not omit the junction or
choose a pairwise assembly order.

## Verification obligations

The existing focused unit tests cover the baseline structural cases:

1. construction with one and more than two named phases;
2. canonical IDs, stable name lookup, and unordered incident-pair lookup;
3. edge orientation independent of phase declaration order;
4. rejection of an empty graph, duplicate names, self-edges, missing phases,
   and duplicate phase pairs;
5. required and rejecting compatibility callbacks;
6. collection of independent configuration errors; and
7. deterministic serialization independent of declaration order.

The approved refinement must additionally cover callback argument orientation
and exact multiplicity, canonical callback order, callback exceptions, valid
and invalid UTF-8, graph-provenance rejection, invalid numeric lookup, reversed
edge orientation, a three-phase cycle, equal- and unequal-law duplicate pairs,
and exhaustive small-graph permutation invariance. Equivalent configurations
must produce the same canonical descriptor ordering, JSON, and callback trace.

Three tests require later components and remain explicit integration
obligations: unchanged phase-system binding while occupancy moves, activation
and disappearance of local geometry pieces without graph mutation, and
failure when geometry detects an unsupported junction. The concrete
multiphase level-set encoding and the decision that a permitted edge is
globally realized belong to geometry. That later layer must map reconstructed
normals to the graph's stored minus-to-plus orientation; the phase graph does
not infer realization from field values.

The [discrete-state contract](02-discrete-state.md) defines how every graph node
receives phase-local fields. The [geometry contract](03-geometry-topology.md)
turns graph identities into current phase regions, and the [workset
contract](05-worksets.md) carries those identities into numerical operators.
