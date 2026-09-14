# Rift

Rift is a C++23 research library for sharp-interface multiphase flow. It uses
[deal.II 9.8.0](https://github.com/dealii/dealii/releases/tag/v9.8.0), MPI,
p4est, simdutf, and spdlog.

This branch provides the first foundation slice: a process-wide context,
canonical phase graphs, immutable mesh snapshots, closed phase supports,
finite-element space schemas and snapshots, immutable transactional state, and
rank-aware logging. It does not yet implement a flow model or time integrator.

## Prerequisites

The setup targets Ubuntu and performs no system-wide installation. The host
must provide a C++23 compiler, CMake 3.21 or newer, GNU Make, curl, tar,
`sha256sum`, and an MPI installation with C and C++ compiler wrappers. Doxygen
and Node.js 22.12 or newer are needed for documentation.

## Local dependencies

Run:

```console
./scripts/install_dependencies.sh
```

The script installs version-pinned simdutf, spdlog, zlib, p4est, deal.II, and
documentation dependencies under `.dependencies/`. Existing successful
installations are reused. The deal.II build can take a substantial amount of
time and disk space.
Useful alternatives are:

```console
./scripts/install_dependencies.sh --check
./scripts/install_dependencies.sh --docs-only
./scripts/install_dependencies.sh --science-only --jobs 12
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

Each `tests/*.cpp` file is automatically built as one same-named CTest using
Boost.UT. Each `tests/mpi/*.cpp` file is built once and registered as one-,
two-, and three-rank CTests using the MPI launcher selected by CMake.

## Rank-aware output

`RiftContext` owns a synchronous logger. By default, informational and more
severe records go to the terminal on world rank zero only. `LoggingOptions`
can disable the terminal, select independent severity thresholds, and enable
one deterministic file per rank from a common base path. Rank zero creates a
missing parent directory before all ranks open their own files. The context
also exposes `pcout()` for deal.II APIs that use stream-style conditional
output; it is separate from logger levels and files.

## Tutorials

Tutorial executables are built by default. The first tutorial walks through
the complete foundation lifecycle with extensive application-oriented
comments:

```console
./build/debug/tutorials/rift_tutorial_01_foundations
mpiexec -n 2 ./build/debug/tutorials/rift_tutorial_01_foundations
```

Set `-DBUILD_TUTORIALS=OFF` when configuring a library-only build.
The Debug and Release test presets register this tutorial at one and two MPI
ranks. Coverage presets disable tutorial compilation so only library and test
translation units contribute instrumentation records.

## Continuous integration and coverage

Pull requests run the unit tests with GCC and Clang. A GCC coverage build uses
gcovr to produce a Cobertura report and uploads it to Codecov. A nightly smoke
workflow restores both compiler-specific deal.II caches and rebuilds them only
if GitHub no longer has them.

## Documentation

```console
cmake -E make_directory build/doxygen
doxygen Doxyfile
npm ci --prefix docs
npm run --prefix docs build
```

Open `docs/dist/index.html` after the build. GitHub Actions builds the site on
pull requests and deploys it after pushes to `main`.
