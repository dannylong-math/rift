# Rift

Rift is a C++23 research library for sharp-interface multiphase flow. The library
is being redesigned; its current public API provides semantic version
information, an owning context with observer dependencies, and a distributed
triangulation owned by `Discretization`. The scientific
dependency setup, build presets, tests, coverage, and documentation pipeline
remain available for new development.

Create a context at the beginning of `main()` and pass it by reference to Rift
objects:

```cpp
#include <rift/context.hpp>

int main(int argc, char **argv)
{
    rift::Context ctx(argc, argv);
    // Create and destroy dependent Rift objects while ctx remains alive.
}
```

The noncopyable, nonmovable context owns MPI lifetime, logging, rank-zero output,
a wall-time timer, and a local phase-index counter. Dependents store mutable
`dealii::ObserverPointer<rift::Context>` members. Observers track lifetime but do
not extend it. Destroy all dependents first, then destroy the context on its
initializing thread.
Construct only one context per program because it initializes the deal.II runtime.
`ctx.id()` is a stable pointer identity for local compatibility checks during
the context's lifetime; it is not a persistent or cross-rank identifier.
Phase registration does not communicate; timer sections synchronize across
the context communicator. Logging policy
is still under design, and timer output is disabled by default. Keep the
context in `main()` until dependent objects and worker activity finish.

`Discretization<2>` and `Discretization<3>` borrow the context and directly own
their deal.II distributed triangulation. Grid generation and refinement require
matching calls on every rank in the context communicator:

```cpp
#include <rift/discretization.hpp>

// While ctx is alive, on every rank:
rift::Discretization<2> discretization(ctx);
discretization.generate_grid("hyper_cube", "0 : 1 : false");
discretization.refine_global(2);
```

The global active-cell count excludes duplicate ghost cells; the local active
count includes ghost and artificial cells. Finite-element spaces, DoF handling,
and solution transfer are not implemented yet.

The previous foundation implementation is preserved at Git tag
`reference/pre-reset-status`. It is historical reference, not a required
architecture for the redesigned library. Inspect it without switching branches:

```console
git show reference/pre-reset-status:tutorials/01_foundations.cpp
git ls-tree -r --name-only reference/pre-reset-status
```

## Prerequisites

The setup targets Ubuntu and performs no system-wide installation. The host
must provide a C++23 compiler, CMake 3.21 or newer, GNU Make, curl, tar,
`sha256sum`, and an MPI installation with C and C++ compiler wrappers. Doxygen
and Node.js 22.12 or newer are needed for documentation.

## Local dependencies

Check prerequisites before installing dependencies:

```console
./scripts/install_dependencies.sh --check
./scripts/install_dependencies.sh
```

The script installs version-pinned simdutf, spdlog, zlib, p4est, deal.II, and
documentation dependencies under `.dependencies/`. Existing successful
installations are reused. The deal.II build can take a substantial amount of
time and disk space. Useful alternatives are:

```console
./scripts/install_dependencies.sh --docs-only
./scripts/install_dependencies.sh --science-only --jobs 6
./scripts/install_dependencies.sh --science-only --variant debug
./scripts/install_dependencies.sh --prefix /path/to/rift-dependencies
```

When using a different prefix, pass the same root to CMake with
`-DRIFT_DEPENDENCIES_DIR=/path/to/rift-dependencies`.

## Build and test

```console
cmake --preset debug
cmake --build --preset debug --parallel 6
ctest --preset debug --output-on-failure
```

The debug preset enables AddressSanitizer and UndefinedBehaviorSanitizer.
`release`, `debug-tidy`, and `release-max` presets are also available.

Each `tests/*_test.cpp` file is automatically built as one same-named CTest using
Catch2 3.16.0. The retained MPI test infrastructure builds each `tests/mpi/*_test.cpp`
file once and registers it as one-, two-, and three-rank CTests using the MPI
launcher selected by CMake. The context test covers observer identity, service
access, local phase registration, and MPI lifetime at all three process counts.
The separate discretization test covers Context borrowing, mesh generation,
refinement, local/global counts, accessors, and generator error propagation in
both two and three dimensions.
The separate context-initialization test checks rejection of an already-started
MPI runtime and verifies that the caller's MPI session remains usable.

## Continuous integration and coverage

Pull requests run the unit tests and coverage checks with GCC and Clang.
Clang source-based coverage provides the required branch gate. GCC uses gcovr
to produce a Cobertura report and uploads it to Codecov. A nightly smoke
workflow restores both compiler-specific deal.II caches and rebuilds them only
if GitHub no longer has them.

Run compiler-specific coverage checks locally with:

```console
./scripts/run_coverage.sh gcc
./scripts/run_coverage.sh clang
```

Coverage requires 100 percent of first-party lines and functions with both
compilers, plus 100 percent of Clang's source branches. GCC raw branch counts
remain visible as diagnostics without a percentage gate or source exclusions
for generated cleanup paths. See [contribution guidance](AGENTS.md) for the
policy, dependencies, and report locations.

## Documentation

```console
cmake -E make_directory build/doxygen
doxygen Doxyfile
npm ci --prefix docs
npm run --prefix docs build
```

Open `docs/dist/index.html` after the build. GitHub Actions builds the site on
pull requests and deploys it after pushes to `main`.
