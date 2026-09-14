# Rift contribution guidance

## Current scope

- Rift is a C++23 research library for sharp-interface multiphase flow.
- This branch implements the Milestone 001 foundation surface: the world-only
  `RiftContext`, immutable phase graph, mesh, support, space, and state
  lifecycles, transactional publication, and rank-aware logging. Do not present
  later solver or geometry-classification designs as implemented behavior.
- The scientific dependency stack is deal.II 9.8.0, simdutf 9.0.0, spdlog
  1.17.0, p4est 2.8.7, zlib 1.3.1, and MPI. Unit tests use Boost.UT 2.3.1, and
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

- `tests/CMakeLists.txt` non-recursively discovers every `tests/*.cpp` file and
  creates one executable and one same-named CTest for it. Adding a normal test
  requires only a new `.cpp` file in that directory.
- All C++ unit tests use Boost.UT. Define named `_test` cases directly in each
  test source and use `expect()` assertions with meaningful comparisons. Do not
  use `assert`, whose checks disappear in Release builds.
- Keep each test source self-contained. Do not recreate the previous physical
  registration headers, subject-runner modes, or test registries.
- Name tests after the behavior or public object they verify, using a `_test.cpp`
  suffix. Keep independent oracles and cover failure behavior as well as the
  expected path.
- `tests/mpi/*.cpp` sources are non-recursively discovered as self-contained
  Boost.UT executables. Each executable is registered through CMake's
  `MPIEXEC_*` variables at one, two, and three ranks; do not introduce custom
  MPI wrappers or test registries.
- Run one test with:

  ```console
  ctest --preset debug -R '^<test_name>$' --output-on-failure
  ```

- Run every MPI variant with:

  ```console
  ctest --preset debug -L mpi --output-on-failure
  ```

- Tutorial sources live under `tutorials/` and are built by default. Run the
  foundation tutorial with `./build/debug/tutorials/rift_tutorial_01_foundations`
  or through the configured MPI launcher at multiple ranks. Debug and Release
  register it as one- and two-rank CTests; coverage presets intentionally set
  `BUILD_TUTORIALS=OFF` so tutorial-only inline instantiations are outside the
  first-party library coverage denominator.

## Coverage

- Coverage scope is first-party code under `include/rift/` and `src/`.
- Required line, function, and branch coverage are each 100 percent for
  in-scope work. Coverage is a gate, not evidence that test oracles are strong.
- Create the ignored project-local coverage environment with:

  ```console
  python3 -m venv .venv
  .venv/bin/python -m pip install gcovr==8.6
  ```

- The supported coverage commands are compiler-specific presets wrapped by one
  script. GCC uses an isolated GCC-built deal.II stack under
  `.dependencies-gcc/`, preserves raw metrics in
  `build/gcc-coverage/coverage-raw-summary.json`, and writes policy-adjusted
  summaries and Cobertura to `build/gcc-coverage/coverage-summary.json` and
  `build/gcc-coverage/coverage.xml`. Clang preserves its raw report in
  `build/clang-coverage/coverage-summary.json` and checks its uncovered source
  locations against the exact approved exclusion list. Both commands enforce
  100 percent policy-adjusted line, function, and branch coverage while still
  printing the raw metrics:

  ```console
  ./scripts/run_coverage.sh gcc
  ./scripts/run_coverage.sh clang
  ```

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
  every pull request and uploads the GCC report to Codecov. Keep this path
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
- Keep user-owned, ignored files under `docs/architecture/` intact. Change them
  only when the user explicitly includes those planning documents in scope.

## Git

- Preserve unrelated user changes and inspect `git status` before editing.
- Work on the current branch unless the user explicitly requests Git state
  changes. Never push or open, merge, or close a pull request; the user handles
  remote Git operations.
