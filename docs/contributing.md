---
title: Contributing
description: The intentionally small test, coverage, and documentation workflow for Rift.
---

# Contributing

Rift is being rebuilt from a clean foundation. Keep each change small enough
that its scientific assumptions, API, implementation, and tests can be reviewed
together.

## Unit tests

The test build intentionally has one rule: every `*_test.cpp` file directly under
`tests/` becomes one same-named executable and CTest. Adding a normal test does
not require editing `tests/CMakeLists.txt`.

Unit tests use Catch2 3.16.0. Keep each test source self-contained, include
`<catch2/catch_test_macros.hpp>`, define named `TEST_CASE` cases, and use `CHECK`
or `REQUIRE` assertions with meaningful comparisons. Ordinary test executables
link `Catch2::Catch2WithMain`, which supplies the test runner. Do not use
`assert`, because Release builds can compile it away.
Name files with a `_test.cpp` suffix and keep each executable focused on one
public object or cohesive behavior. The earlier registration-header and
subject-runner system is intentionally not part of this simpler layout.

```console
cmake --preset debug
cmake --build --preset debug --parallel 6
ctest --preset debug --output-on-failure
```

Run one test with:

```console
ctest --preset debug -R '^version_test$' --output-on-failure
```

The retained MPI infrastructure discovers each `tests/mpi/*_test.cpp` source and
registers one-, two-, and three-rank CTests through CMake's selected launcher.
There are currently no MPI tests. Future MPI tests link `Catch2::Catch2` and
provide an MPI-aware `main` that runs `Catch::Session`.

```console
ctest --preset debug -L mpi --output-on-failure
```

## Coverage

Pull requests run a GCC coverage build after the GCC and Clang unit tests pass.
gcovr 8.6 checks line, function, and branch coverage for first-party code under
`include/rift/` and `src/`, writes `coverage.xml` in Cobertura format, and the
workflow uploads that file to Codecov.

Run compiler-specific coverage reports locally with:

```console
./scripts/run_coverage.sh gcc
./scripts/run_coverage.sh clang
```

The scripts use the matching coverage presets and write reports under
`build/gcc-coverage/` and `build/clang-coverage/`. GCC requires the isolated
scientific dependency stack in `.dependencies-gcc/`; see the repository's
`AGENTS.md` for the setup commands. The version-only baseline has no branches
and no coverage exclusions.

The three coverage thresholds are 100 percent for in-scope code. Coverage is a
useful completeness check, but meaningful independent test oracles remain
necessary.

The nightly dependency-cache smoke workflow restores the same GCC and Clang
caches used by pull requests, rebuilding only after a cache miss, and then runs
the normal build and test. This keeps the expensive deal.II installations warm
and detects a stale or unusable cache.

## Documentation and formatting

Document public C++ declarations with Doxygen block comments. Explain parameters,
return values, failures, ownership, and important invariants when applicable.

```console
cmake -E make_directory build/doxygen
doxygen Doxyfile
npm ci --prefix docs
npm run --prefix docs build
```

Run `./format.sh` before submitting C++ changes.
