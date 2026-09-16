# Rift contribution guidance

## Current scope

- Rift is a C++23 research library for sharp-interface multiphase flow.
- The library is being redesigned. Its current public APIs are semantic
  version information in `rift/version.hpp`, the owning context in
  `rift/context.hpp`, and basic distributed mesh operations in
  `rift/discretization.hpp`; previous foundation APIs and tutorials have been removed.
  Do not present historical designs as implemented behavior.
- The previous implementation is preserved at Git tag
  `reference/pre-reset-status`. Consult it with
  `git show reference/pre-reset-status:<path>` or list its files with
  `git ls-tree -r --name-only reference/pre-reset-status`. Its architecture and
  APIs are historical reference, not requirements for the redesign.
- `docs/development/foundations/` and
  `docs/development/first-pde-demonstration/` contain archived plans, not active
  implementation instructions.
- The scientific dependency stack is deal.II 9.8.0, simdutf 9.0.0, spdlog
  1.17.0, p4est 2.8.7, zlib 1.3.1, and MPI. Unit tests use Catch2 3.16.0, and
  Sourcey 3.6.5 builds the documentation.
- Rift is pre-release software. Source and ABI compatibility are not yet gates,
  but update every in-repository caller, test, and document with an API change.

## Build and dependencies

- Check host prerequisites without installing anything with:

  ```console
  ./scripts/install_dependencies.sh --check
  ```

- Dependency installation writes only under `.dependencies/` or an explicitly
  selected project-local prefix such as `.dependencies-gcc/`. It is expensive,
  may download archives, and may run `npm ci`; do not run it merely for
  discovery or without the user's approval.
- Configure, build, and test Debug with:

  ```console
  cmake --preset debug
  cmake --build --preset debug --parallel 6
  ctest --preset debug --output-on-failure
  ```

- Use at most six build jobs by default because deal.II compilation can exhaust
  this workstation's memory. Do not exceed eight without explicit approval.
- Replace `debug` with `release` for Release verification. Use `debug-tidy` for
  clang-tidy. CMake regenerates the root `compile_commands.json` symlink.

## Tests

- `tests/CMakeLists.txt` non-recursively discovers every `tests/*_test.cpp` file and
  creates one executable and one same-named CTest for it. Adding a normal test
  requires only a new `*_test.cpp` file in that directory.
- All C++ unit tests use Catch2 3.16.0. Include
  `<catch2/catch_test_macros.hpp>`, define named `TEST_CASE` cases, and use
  `CHECK` or `REQUIRE` with meaningful comparisons. Ordinary test executables
  link `Catch2::Catch2WithMain`; do not supply a separate `main`. Do not use
  `assert`, whose checks disappear in Release builds.
- Keep each test source self-contained. Do not recreate the previous physical
  registration headers, subject-runner modes, or test registries.
- Name tests after the behavior or public object they verify, using a `_test.cpp`
  suffix. Keep independent oracles and cover failure behavior as well as the
  expected path.
- The retained MPI infrastructure non-recursively discovers `tests/mpi/*_test.cpp`
  sources and registers each executable through CMake's `MPIEXEC_*` variables
  at one, two, and three ranks. MPI test executables link `Catch2::Catch2` and
  provide an MPI-aware `main` that runs `Catch::Session`; do not introduce
  custom MPI wrappers or test registries. The context test creates its Rift
  context around `Catch::Session` so the context owns MPI initialization and
  finalization.
- `context_initialization_test.cpp` deliberately initializes MPI directly in
  its own executable, then verifies that Context rejects external initialization
  without finalizing the caller's MPI session. Keep this rejection test separate
  from normal Context-owned runtime tests.
- GitHub CI uses Open MPI with `-DMPIEXEC_PREFLAGS=--oversubscribe` and runs
  CTest with `--parallel 1`, allowing the three-rank test on two-core runners.
  Keep MPI unit tests small in memory and runtime; oversubscribed runs provide
  correctness evidence, not performance evidence. Keep this launcher flag in
  the Open MPI CI configurations rather than shared presets, since local MPI
  implementations may not support it.
- Run one test with:

  ```console
  ctest --preset debug -R '^<test_name>$' --output-on-failure
  ```

- Run every MPI variant with:

  ```console
  ctest --preset debug -L mpi --output-on-failure
  ```

## Coverage

- Coverage scope is first-party code under `include/rift/` and `src/`.
- GCC and Clang must both pass builds and tests and achieve 100 percent line
  and function coverage for in-scope work. Clang source-based branch coverage
  must also be 100 percent and is the authoritative branch gate.
- Report GCC raw branch coverage without a percentage gate. GCC includes
  compiler-generated exception and cleanup edges; retain these counts without
  filtering or marking source lines merely to reach a percentage. This
  compiler-specific policy supersedes a blanket requirement for 100 percent
  GCC branches.
- Coverage is a gate, not evidence that test oracles are strong. Test documented
  failure behavior, state preservation, and custom resource cleanup explicitly.
  Keep independent oracles and sanitizer checks. Source exclusions remain
  exceptional: require a narrowly identified, demonstrably unreachable path,
  written justification, source location, tool-specific suppression, and user
  review approval.
- Create the ignored project-local coverage environment with:

  ```console
  python3 -m venv .venv
  .venv/bin/python -m pip install gcovr==8.6
  ```

- Run the supported compiler-specific coverage presets through:

  ```console
  ./scripts/run_coverage.sh gcc
  ./scripts/run_coverage.sh clang
  ```

- GCC uses an isolated GCC-built deal.II stack under `.dependencies-gcc/`.
  Raw GCC metrics are in `build/gcc-coverage/coverage-raw-summary.json`;
  policy-adjusted metrics and Cobertura are in `coverage-summary.json` and
  `coverage.xml` in that directory. Clang writes `coverage.json`,
  `coverage.lcov`, and `coverage-summary.json` under `build/clang-coverage/`.
  Both commands run CTest with `--parallel 1`, require nonempty first-party
  line/function coverage, and enforce their compiler-specific gates above.
  Clang source discovery includes nested headers and source files. A zero
  branch denominator is reported as not applicable. There are no approved
  coverage exclusions, so raw and policy-adjusted metrics are identical;
  diagnostic GCC branches remain in the JSON and Cobertura reports.

- Build the isolated GCC scientific dependencies once, with the system OpenMPI
  wrappers selected consistently across C, C++, Fortran, and the launcher:

  ```console
  env -u CMAKE_PREFIX_PATH -u LD_LIBRARY_PATH -u PETSC_DIR -u PETSC_ARCH \
    CC=/usr/bin/gcc CXX=/usr/bin/g++ FC=/usr/bin/gfortran \
    OMPI_CC=/usr/bin/gcc OMPI_CXX=/usr/bin/g++ OMPI_FC=/usr/bin/gfortran \
    MPI_C_COMPILER=/usr/bin/mpicc.openmpi \
    MPI_CXX_COMPILER=/usr/bin/mpicxx.openmpi \
    MPI_Fortran_COMPILER=/usr/bin/mpif90.openmpi \
    MPIEXEC_EXECUTABLE=/usr/bin/mpiexec.openmpi \
    ./scripts/install_dependencies.sh --prefix .dependencies-gcc \
      --science-only --variant debug --jobs 6
  ```

- `.github/workflows/ci.yml` runs the full GCC and Clang unit-test matrix on
  every pull request, then the Clang coverage gate. The existing `Coverage`
  job depends on the Clang gate, enforces GCC line/function coverage, and
  uploads the unfiltered GCC report to Codecov. Clang CI reuses
  `./scripts/run_coverage.sh clang` with `RIFT_COVERAGE_JOBS=2` and configures
  the preset with `-DMPIEXEC_PREFLAGS=--oversubscribe`. Keep this path
  simple; do not add custom profile mergers, manifest generators, or parallel
  coverage runners unless a demonstrated limitation requires one.
- `.github/workflows/cache-keepalive.yml` restores the GCC and Clang scientific
  dependency caches nightly and runs a smoke build/test. It rebuilds a missing
  cache so pull requests can reuse a default-branch cache.

## Documentation and formatting

- Public C++ declarations use `/** ... */` Doxygen comments. Document parameters,
  return values, failure behavior, ownership, and important invariants where
  applicable.
- Build the documentation with:

  ```console
  cmake -E make_directory build/doxygen
  doxygen Doxyfile
  npm ci --prefix docs
  npm run --prefix docs build
  ```

- Run `./format.sh` before submitting C++ changes.
- Keep user-owned, ignored files under `docs/architecture/` and `_planning/`
  intact. Change them only when the user explicitly includes those planning
  documents in scope.

## Git

- Preserve unrelated user changes and inspect `git status` before editing.
- Work on the current branch unless the user explicitly requests Git state
  changes. Never push or open, merge, or close a pull request; the user handles
  remote Git operations.
