---
title: Introduction
description: Build Rift and explore its architecture and generated C++ API reference.
---

# Rift

Rift is a C++23 research code for sharp-interface multiphase flow. It uses
[deal.II 9.8.0](https://github.com/dealii/dealii/releases/tag/v9.8.0), MPI, and
p4est.

The project is at an early stage. Its documentation records the contracts and
design decisions that will guide the implementation, alongside a C++ API
reference generated from the public headers.

For a practical path through the functionality that exists today, start with
[Tutorial 1](tutorials/tutorial-001.md), continue to
[Tutorial 2](tutorials/tutorial-002.md), and finish with
[Tutorial 3](tutorials/tutorial-003.md). Their code is compiled and run with
MPI as part of the normal test suite. The sections below are deeper reference
guides for the same objects.

## Build and test

Install the local scientific dependencies, then configure and build Rift from
the repository root:

```shell
./scripts/install_dependencies.sh --science-only
cmake --preset debug
cmake --build --preset debug --parallel 6
ctest --preset debug --output-on-failure
```

The debug preset enables AddressSanitizer and UndefinedBehaviorSanitizer.
Release, `debug-tidy`, and `release-max` presets are also available.

## Create a run and phase graph

Run-owned Rift objects start from a collective `RunConfiguration`. It gives
every participating rank the same run identity and retains an owned duplicate
of the supplied communicator. The caller may therefore release its original
communicator after `create()` succeeds. Rift run, mesh, and state collectives
use one local process group, so `create()` requires an MPI intracommunicator
and rejects intercommunicators before duplication or identity allocation. The
communicator must be derived entirely from one `MPI_COMM_WORLD`: Rift validates
every communicator member, then encodes the world rank of communicator rank
zero and its local monotonic sequence into the run ID. Disjoint and overlapping
communicators therefore cannot reuse an identity within one MPI execution.
Intercommunicators and communicators containing members from unrelated
dynamic-process or MPI Sessions process worlds are rejected. MPI operation
failures invoke the fatal MPI handler because ranks cannot safely recover
independently after a failed collective operation.

```cpp
#include <mpi.h>
#include <rift/phase_graph.hpp>
#include <rift/run_configuration.hpp>

#include <iostream>

int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);
  {
    auto run_result = rift::RunConfiguration::create(MPI_COMM_WORLD);
    if (!run_result) {
      std::cerr << run_result.error().message << '\n';
      MPI_Abort(MPI_COMM_WORLD, 1);
    }

    auto graph_result = rift::make_phase_graph(
        *run_result, {{"gas", "compressible"}}, {});
    if (!graph_result) {
      for (const auto &error : graph_result.error())
        std::cerr << error.message << '\n';
      MPI_Abort(MPI_COMM_WORLD, 1);
    }

    const auto gas_id = graph_result->find_phase("gas").value();
    const auto gas = graph_result->reference(gas_id).value();
    std::cout << graph_result->phase(gas)->get().name << '\n';
  }
  MPI_Finalize();
}
```

`PhaseId` is local to one graph construction. Export `PhaseReference` instead
when an ID crosses a component boundary: it carries both run and graph-instance
provenance, allowing the receiver to reject values from a different graph.
Graph lookups and reference checks are local after collective construction.
Release all run-owned objects before `MPI_Finalize()`.

### Configure interfaces collectively

Every rank supplies the same logical phase graph. Declaration order does not
matter: Rift canonicalizes phases and interfaces before comparing the complete
input byte for byte. Names and runtime keys must be valid RFC 3629 UTF-8. Rift
preserves their exact bytes, sorts by unsigned UTF-8 bytes, and deliberately
does not normalize Unicode or fold case. For example, precomposed `"é"` and
the visually similar decomposed spelling `"e\u0301"` remain distinct names.

Provide one interface declaration for each unordered pair of phases. Its
minus/plus order is retained as the physical orientation, while a second
declaration for the same unordered pair is rejected even if it names a
different law. Put multiple physical effects for that pair behind one composite
interface-operator key.

```cpp
auto graph_result = rift::make_phase_graph(
    run,
    {{"gas", "compressible"}, {"liquid", "low-mach"}},
    {{"surface", "liquid", "gas", "capillary-and-phase-change"}},
    [](const rift::PhaseDescriptor &minus_phase,
       const rift::PhaseDescriptor &plus_phase,
       const rift::InterfaceSpecification &interface_specification)
        -> std::optional<std::string> {
      if (minus_phase.physics_key.value() != "low-mach")
        return "the minus phase must use the low-mach model";

      return std::nullopt;
    });
```

Compatibility callbacks run once per structurally valid unique edge, in
canonical interface-name order, and receive the declared minus descriptor,
plus descriptor, and interface specification. All ranks must return the same
accepted flag and exact reason. An ordinary rejection becomes a structured
graph-configuration error. If a callback throws, ranks finish the collective
exception check before any rank advances: the throwing rank rethrows its
original exception, while other ranks throw `std::runtime_error` with a
deterministic remote-callback message. Any nonempty interface list requires a
callback, including a list whose phase references are themselves invalid.

## Transfer a mesh into an immutable snapshot

Use `make_mesh_snapshot()` after constructing a completed triangulation and
before building spaces or state. The factory consumes the `unique_ptr` on
every outcome. A successful snapshot exposes only a const triangulation view,
so later run-owned objects can share one topology without depending on an
application-owned mutable mesh.

For a distributed mesh, every run rank calls the factory in the same order and
passes a non-null triangulation constructed on the same MPI communicator
context. That communicator must be identical or congruent to the run
communicator; equal groups in a different rank order are rejected. MPI cannot
portably detect ranks alternating distinct but congruent communicator
contexts, so using the same context on every rank is a collective-call
precondition. The transferred mesh must also have no external mutators or
signal subscribers: deal.II deliberately does not copy signal connections.

Rift currently supports serial `dealii::Triangulation` and nonempty
`dealii::parallel::distributed::Triangulation` in 2D and 3D. deal.II 9.8
borrows the communicator passed to a distributed triangulation. Rift therefore
duplicates that communicator and reconstructs the distributed triangulation
on the owned duplicate. This lets the caller release its original
communicator and `RunConfiguration` handle while the snapshot remains alive.
Empty distributed meshes and other parallel triangulation kinds are rejected
instead of silently changing their type or partitioning semantics.

```cpp
#include <deal.II/base/mpi.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <mpi.h>
#include <rift/mesh_snapshot.hpp>
#include <rift/run_configuration.hpp>

#include <iostream>
#include <memory>

int main(int argc, char **argv) {
  dealii::Utilities::MPI::MPI_InitFinalize mpi(argc, argv, 1);

  MPI_Comm source = MPI_COMM_NULL;
  if (MPI_Comm_dup(MPI_COMM_WORLD, &source) != MPI_SUCCESS)
    return 1;

  std::shared_ptr<const rift::MeshSnapshot<2>> snapshot;
  {
    auto run_result = rift::RunConfiguration::create(source);
    if (!run_result) {
      std::cerr << run_result.error().message << '\n';
      return 1;
    }

    std::unique_ptr<dealii::Triangulation<2>> mesh =
        std::make_unique<
            dealii::parallel::distributed::Triangulation<2>>(source);
    dealii::GridGenerator::hyper_cube(*mesh);

    auto snapshot_result =
        rift::make_mesh_snapshot(*run_result, std::move(mesh));
    if (!snapshot_result) {
      std::cerr << snapshot_result.error().message << '\n';
      return 1;
    }
    snapshot = *snapshot_result;
  }

  if (MPI_Comm_free(&source) != MPI_SUCCESS)
    return 1;

  std::cout << "mesh " << snapshot->id().value() << " has "
            << snapshot->triangulation().n_global_active_cells()
            << " active cells\n";
}
```

`MeshSnapshotProvenance` pairs the globally unique mesh identity with the run
that authorized construction. Compare the full provenance value at object
boundaries; a raw numeric mesh ID alone is not a compatibility proof. Mesh IDs
use the run origin world rank and a process-global per-origin sequence, so
separately consumed equal-topology meshes and meshes created through disjoint
or overlapping run communicators remain distinct during one MPI execution.
Logical validation errors are returned collectively. MPI operation failures
and rank-local post-validation allocation or reconstruction failures are fatal
because ranks cannot safely resume independently.

## Build the documentation

Install Doxygen, Node.js 22.12 or newer, and the pinned Sourcey dependencies.
Then generate Doxygen XML and build the static site:

```shell
cmake -E make_directory build/doxygen
doxygen Doxyfile
npm ci --prefix docs
npm run --prefix docs build
```

The generated site is written to `docs/dist/`.
