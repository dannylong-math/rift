---
title: Contributing
description: The intentionally small test, coverage, and documentation workflow for Rift.
---

# Contributing

Rift is being rebuilt from a clean foundation. Keep each change small enough
that its scientific assumptions, API, implementation, and tests can be reviewed
together.

## Unit tests

The test build intentionally has one rule: every `.cpp` file directly under
`tests/` becomes one same-named executable and CTest. Adding a normal test does
not require editing `tests/CMakeLists.txt`.

Unit tests use Boost.UT. Keep each test source self-contained, define named
`_test` cases directly in it, and use `expect()` assertions with meaningful
comparisons. Do not use `assert`, because Release builds can compile it away.
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

There is currently no MPI test harness. Multi-rank testing will be designed
when the first MPI-dependent feature needs it.

## Coverage

Pull requests run a GCC coverage build after the GCC and Clang unit tests pass.
gcovr 8.6 checks line, function, and branch coverage for first-party code under
`include/rift/` and `src/`, writes `coverage.xml` in Cobertura format, and the
workflow uploads that file to Codecov.

Run the same report locally with:

```console
cmake -S . -B build/coverage -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DENABLE_SANITIZERS=OFF \
  -DENABLE_COVERAGE=ON
cmake --build build/coverage --parallel 6
ctest --test-dir build/coverage --output-on-failure
gcovr --root . \
  --filter 'include/rift/' \
  --filter 'src/' \
  --print-summary \
  --fail-under-line 100 \
  --fail-under-function 100 \
  --fail-under-branch 100 \
  --cobertura-pretty \
  --output coverage.xml \
  build/coverage
```

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
