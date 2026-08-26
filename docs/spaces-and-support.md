---
title: Build finite-element spaces
description: Define owner-local phase support, close it across hanging faces, and finalize an immutable Rift space generation.
---

# Build finite-element spaces

[Tutorial 2](tutorials/tutorial-002.md) is the recommended first practical
example. It runs on two MPI ranks and derives every displayed code block from
the compiled tutorial source. This page then gives the detailed support,
closure, provenance, and error contracts.

A `SpaceRegistry` turns three different kinds of input into one immutable
finite-element generation:

- the phase graph is the authority for phase identities;
- the mesh snapshot is the authority for topology, ownership, and MPI
  communication; and
- the space specification describes the replicated field schema while phase
  support records describe rank-local data.

Keeping the replicated schema separate from owner-local support is important
in distributed runs. Every rank names the same fields in any declaration
order, but each rank requests only cells that it owns. Rift compares a
canonical encoding of the schema collectively, merges diagnostics into the
same byte sequence on every rank, and then closes phase supports.

## Compact serial reference example

The following program creates one phase field on the complete serial mesh.
The same API is used for a distributed mesh; only each rank's support set is
different.

```cpp
#include <deal.II/base/mpi.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria.h>
#include <rift/discrete_state.hpp>
#include <rift/mesh_snapshot.hpp>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>

#include <iostream>
#include <memory>

int main(int argc, char **argv) {
  dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);

  auto run_result = rift::RunConfiguration::create(MPI_COMM_SELF);
  if (!run_result) {
    std::cerr << run_result.error().message << '\n';
    return 1;
  }
  auto &run = *run_result;

  auto graph_result =
      rift::make_phase_graph(run, {{"gas", "compressible"}}, {});
  if (!graph_result) {
    std::cerr << graph_result.error().front().message << '\n';
    return 1;
  }
  auto &graph = *graph_result;
  const auto gas_id = graph.find_phase("gas").value();
  const auto gas = graph.reference(gas_id).value();

  auto triangulation = std::make_unique<dealii::Triangulation<2>>();
  dealii::GridGenerator::hyper_cube(*triangulation);
  auto mesh_result =
      rift::make_mesh_snapshot(run, std::move(triangulation));
  if (!mesh_result) {
    std::cerr << mesh_result.error().message << '\n';
    return 1;
  }
  const auto mesh = *mesh_result;

  rift::SupportEnvelope requested;
  for (const auto &cell : mesh->triangulation().active_cell_iterators())
    if (cell->is_locally_owned())
      requested.insert(cell->id());

  rift::SpaceSpecification specification{
      .phase_fields = {{gas, "flow", 4, 1}},
      .level_set = {"level_sets", 1, 1},
  };
  std::vector<rift::PhaseSupportSpecification> supports{{
      .phase = gas,
      .mesh = mesh->id(),
      .locally_owned_requested_cells = std::move(requested),
  }};

  rift::SpaceRegistry<2> registry(mesh);
  auto draft =
      registry.begin_draft(graph, std::move(specification), std::move(supports));
  if (!draft) {
    for (const auto &error : draft.error())
      std::cerr << error.message << '\n';
    return 1;
  }

  auto space = registry.finalize(*draft, {{"mean_pressure"}});
  if (!space) {
    for (const auto &error : space.error())
      std::cerr << error.message << '\n';
    return 1;
  }

  const auto flow_id = space->find_field(gas, "flow").value();
  const auto &flow = space->field_space(gas, flow_id);
  std::cout << "flow DoFs: " << flow.dof_handler().n_dofs() << '\n';
}
```

## Supply one support record per represented phase

Every rank supplies exactly one `PhaseSupportSpecification` for every phase
that appears in `phase_fields`. An empty owner-local set is valid and is still
represented by a record. A missing, duplicate, or unused support record is a
configuration error. The phase must be a `PhaseReference` owned by the exact
graph, and every cell ID must belong to the exact mesh snapshot and be active
and locally owned by the calling rank.

`finalize()` is transactional and accepts only an active draft lvalue. A
logical regional-schema or foreign-registry error leaves the draft active and
unchanged, so the caller can correct the input or retry with its originating
registry. Success alone consumes the draft; `draft.active()` then returns
false. Reusing a moved-from or already finalized draft reports
`inactive_draft` collectively.

All field groups for one phase share the same `PhaseSupport` object. Its three
masks distinguish what the application requested, what hanging-face closure
added, and the final owner-local support used to select finite elements. This
makes closure additions visible without duplicating support policy in every
field.

## Understand adaptive closure and DoF ownership

On a hanging interface, activating either the coarse cell or one adjacent fine
cell requires the coarse cell and all fine siblings touching that face. Rift
repeats owner-to-ghost publication, ghost-to-owner requests, and a global
changed test until no support grows. The operation is a monotone least fixed
point, so input declaration and rank traversal order do not affect the final
mask.

Only locally owned active cells receive an active finite-element index.
Supported cells use the real continuous-Galerkin element; unsupported cells
use component-compatible, non-dominating `FE_Nothing`. deal.II synchronizes
the owner-selected indices to ghost cells during DoF distribution. Rift never
queries artificial-cell FE state. Hanging constraints are built over locally
relevant DoFs, while the level-set group always spans the full background
mesh.

## Treat provenance as a complete compatibility key

Drafts, field spaces, level-set spaces, snapshots, and state layouts expose
the same `SpaceProvenance`: run, graph instance, mesh snapshot, registry, and
epoch. Compare the whole value before reusing cached indices. A bare epoch or
numeric phase ID is not sufficient.

Epoch reservation is collective and happens before logical validation, so a
rejected draft still consumes its epoch. This prevents stale provisional data
from aliasing a later accepted generation. A registry can finalize only its
own drafts.

Logical input mistakes return `SpaceBuildErrors` identically on every rank.
MPI status failures, deal.II failures during collective construction, and
rank-local allocation failures invoke the run's fatal handler because peers
cannot safely resume at different points in the collective sequence.
