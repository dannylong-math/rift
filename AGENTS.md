# Rift contribution guidance

## Unit tests

- Put C++ unit tests in `tests/`; CMake discovers every `.cpp` file there.
- Keep each test file small, self-contained, and focused on one behavior.
- Prefer several readable files over one large fixture-heavy suite.
- Name files `<subject>_<behavior>_<two-digit-sequence>.cpp`, for example
  `foo_bar_00.cpp`, `foo_bar_01.cpp`, and `phase_graph_lookup_00.cpp`.
- Use a shared header only when it removes incidental setup without hiding the
  behavior under test.
- Calculate line, function, and branch coverage after every implementation.
  Project code in `include/` and `src/` must have 100% coverage for all three
  metrics before the work is complete.
- When any metric is below 100%, inspect the report for the exact uncovered
  behavior and add small, focused unit tests that exercise it. Run the
  complete test suite and coverage report again until both pass and all three
  coverage metrics reach 100%.

`docs/contributing.md` is the canonical human-facing guide. Keep it
synchronized with approved policy changes; the stricter agent gates in this
file apply until that synchronization is separately authorized and completed.

## C++ documentation

- Document every C++ entity, including private members, file-local helpers,
  and types in `rift::detail`.
- Write Doxygen comments as `/** ... */` blocks. Do not use `///` comments.
- Document template parameters, function parameters, return values, and thrown
  exceptions where applicable.
- Write `\brief` from the reader's perspective: state what the entity helps the
  reader accomplish rather than merely restating its declaration.
- Use a structured guided-reference style in extended descriptions. Add
  `\par When to use`, `\par Typical use`, `\par Important behavior`, and
  `\par Failure handling` when those sections are relevant.
- Give every class and struct a realistic, complete `\code{.cpp}` example in
  its extended description. Show the full intended workflow rather than an
  isolated expression or pseudocode, and use only APIs that actually exist.
- For standalone functions and important entry points, include a complete
  example. For small members, explain their place in the class workflow and
  use a focused snippet instead of copying the full class example.
- Write public documentation for library users. Write private, file-local, and
  `rift::detail` documentation for maintainers, explaining the internal usage
  pattern and the invariant the entity preserves.
- Prefer explanations of ownership, lifetime, error handling, and important
  invariants over descriptions of implementation syntax visible in the
  declaration itself.
- Keep Doxygen configured to extract private, static, anonymous-namespace, and
  detail-namespace implementation documentation and to fail on missing docs.

## Project overview

- Rift is a C++23 research library for sharp-interface multiphase flow.
- The current implementation covers the runtime phase graph and discrete-state
  foundations. The architecture pages describe later solver components; do
  not present those components as implemented or verified.
- The accepted initial methods are continuous Galerkin on an adaptively
  refined background mesh, full-background level sets, pairwise material
  interfaces, MPI plus CPU SIMD, IMEX-ARK production integration, and Radau
  IIA verification integration.
- The supported dependency stack is deal.II 9.8.0, p4est 2.8.7, zlib 1.3.1,
  MPI, Boost.UT 2.3.1, and Sourcey 3.6.5.
- GPU execution, DG, full `hp` adaptivity, PETSc, and Trilinos are deferred
  extension points, not supported backends.
- Rift is in pre-release development. Source API and ABI stability are not
  gates: approved work may make breaking changes without compatibility shims,
  deprecation periods, or ABI checks. Update all callers, tests, and
  documentation in the same change. Perform a separate API/ABI stabilization
  exercise before the initial release.
- Rift currently builds a static library and has no install/export package,
  shared-library baseline, plugin interface, or language binding.
- `PhaseGraph::canonical_json()` is output-only schema `rift.phase_graph`,
  version 1. No restart or checkpoint format is implemented.

## Build and dependencies

- Check host prerequisites without installation with
  `./scripts/install_dependencies.sh --check`.
- Dependency installation writes under `.dependencies/`, downloads archives,
  builds third-party software, and may run `npm ci`; do not run it merely for
  discovery and do not install new dependencies without user approval.
- Configure, build, and test Debug with:

  ```console
  cmake --preset debug
  cmake --build --preset debug --parallel
  ctest --preset debug --output-on-failure
  ```

- Replace `debug` with `release` for Release verification. Also run Release
  for public-interface, numerical, ownership, concurrency, or
  optimization-sensitive changes.
- Run clang-tidy during compilation with:

  ```console
  cmake --preset debug-tidy
  cmake --build --preset debug-tidy --parallel
  ```

  A discoverable `clang-tidy` executable is required.
- Clean a configured preset with
  `cmake --build --preset <preset> --target clean`.
- No CMake workflow presets or source-tree Makefile targets are defined.
- CMake configuration regenerates the root `compile_commands.json` symlink;
  do not edit it manually.

## Focused and MPI tests

- Run one registered unit test with
  `ctest --preset debug -R '^<exact_test_name>$' --output-on-failure`.
- Current tests run as single processes and generally use `MPI_COMM_SELF`.
- Put genuinely multi-rank tests in `tests/mpi/`. Register them explicitly
  through CMake's MPI launcher variables, including a declared rank count; do
  not hard-code `mpirun` or `mpiexec`.
- The current recursive test-source glob would otherwise register
  `tests/mpi/*.cpp` as ordinary serial tests. When the first MPI test is added,
  update test registration to keep serial and MPI tests distinct.
- The blocking CI matrix for MPI-capable work is staged:
  - serial unit tests with GCC and Clang;
  - MPI tests with OpenMPI/GCC and MPICH/GCC;
  - two ranks by default, with three or four only when the tested contract
    requires them; and
  - the full compiler-by-MPI cross-product only in manual or extended CI.
- This MPI matrix and its exact commands are policy, not current repository
  capability, until the CMake helpers and CI jobs are implemented.

## Coverage policy and exclusions

- Coverage scope is first-party code under `include/` and `src/`.
- Required line coverage: 100%.
- Required function coverage: 100%.
- Required branch coverage: 100%.
- Generate the Clang coverage build and report with:

  ```console
  cmake -S . -B build/coverage -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DBUILD_TESTING=ON \
    -DENABLE_SANITIZERS=OFF \
    -DENABLE_COVERAGE=ON
  cmake --build build/coverage --parallel
  ctest --test-dir build/coverage --output-on-failure
  rift_root="$(pwd)"
  gcovr \
    --root "${rift_root}" \
    --filter "${rift_root}/include/" \
    --filter "${rift_root}/src/" \
    --exclude "${rift_root}/build/" \
    --gcov-executable "$(clang -print-prog-name=llvm-cov) gcov" \
    --fail-under-line 100 \
    --fail-under-function 100 \
    --fail-under-branch 100 \
    --print-summary \
    build/coverage
  ```

- Report raw metrics by repeating the gcovr report with
  `--exclude-pattern-prefix RIFT_RAW_NO_EXCLUSIONS`. Report both raw and
  policy-adjusted metrics.
- The exclusion register is
  `docs/development/quality/coverage-exclusions.md`. Create it when first
  needed.
- Use one register record per exclusion root cause; one record may cover
  several source locations. Each record has a stable `COV-###` ID, status
  (`Proposed`, `Approved`, or `Retired`), locations, compiler and coverage-tool
  details, evidence that the code is demonstrably unreachable or an artifact,
  rationale, approver, approval date, and reevaluation trigger.
- Agents may add `Proposed` records. Only the user may change a record to
  `Approved`. Do not add a new gcovr suppression marker or count an exclusion
  in policy-adjusted coverage until its record is approved; reference the
  approved `COV-###` ID beside the marker.
- Treat existing unregistered `GCOVR_EXCL_LINE` markers as `Proposed`, not
  approved. Audit them before the next managed feature is declared complete.
- Coverage is a gate, not evidence that test oracles and properties are
  adequate.

## Warnings, static analysis, sanitizers, and formatting

- First-party compiler and clang-tidy warnings are errors for completion. The
  current build configuration does not fully enforce that policy; report and
  close the enforcement gap within the scope of work that first depends on it.
- Fix diagnostics by default. If a clang-tidy check is broadly too strict or
  noisy, propose a documented change to `.clang-tidy` with examples and
  rationale for user review; do not disable it unilaterally.
- A narrowly scoped `NOLINT(<check>)` is allowed only for a genuine isolated
  false positive, with an inline explanation and explicit user approval.
- The blocking sanitizer matrix is staged:
  - serial Debug tests under ASan and UBSan;
  - blocking OpenMPI and MPICH tests without sanitizers initially;
  - MPI ASan and UBSan in manual or extended CI until launcher behavior and
    any suppressions are validated, then promotion may be proposed;
  - TSan deferred until first-party threaded code exists; and
  - MSan deferred because the dependency stack requires compatible
    instrumentation.
- Do not introduce sanitizer suppressions without the same evidence and user
  review expected for coverage exclusions.
- Check formatting without modifying files with:

  ```console
  find include src tests -type f \
    \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) \
    -exec clang-format --dry-run --Werror {} +
  ```

- Apply formatting with `./format.sh`; it rewrites tracked source and test
  files.

## Adversarial testing

- Property-based testing framework and command: TBD.
- Fuzzer, corpus, and reproducer conventions: TBD.
- Mutation-testing tool and command: TBD.
- Do not install adversarial tools unilaterally. The test skeptic reports
  missing tools as explicit gaps and proposes repository-compatible options
  when a feature would benefit from them.

## Documentation and development records

- Build Doxygen XML and the Sourcey site with:

  ```console
  cmake -E make_directory build/doxygen
  doxygen Doxyfile
  npm ci --prefix docs
  npm run --prefix docs build
  ```

- Doxygen fails on warnings and missing entity documentation. Do not edit
  generated Doxygen XML, `docs/node_modules/`, or `docs/dist/` manually.
- Architecture contracts live in numbered pages under `docs/architecture/`;
  accepted-decision status lives in
  `docs/architecture/24-decision-status.md`.
- Changes to accepted architecture require user approval, updates to every
  affected architecture page, and corresponding conformance tests.
- Store committed feature evidence under
  `docs/development/features/<slug>/`. Create only applicable files from:
  `plan.md`, `verification.md`, `vv.md`, `performance.md`, and `pr-report.md`.
- These development records remain outside Sourcey navigation unless the user
  deliberately publishes them. Do not use ignored `_planning/` for records
  that must be committed.
- No bibliography, citation-file, or Zotero convention exists yet.

## Performance and V&V

- Google Benchmark is the selected benchmark framework, but do not add or
  install it until performance optimization is requested.
- Benchmark targets, representative inputs, hardware/runtime controls,
  regression thresholds, and report commands are deferred until that work.
- `release` uses `-O3 -march=native`; benchmark results and binaries are
  host-specific.
- `release-max` additionally permits floating-point reassociation, reciprocal
  approximations, and changed signed-zero behavior. Do not use it as a
  correctness or benchmark baseline without explicit numerical approval.
- Numerical V&V conventions, context of use, canonical problems, validation
  datasets, uncertainty records, adopted standards, and commands are deferred
  until V&V work begins. Unit tests and coverage never substitute for V&V.

## Local Git workflow

- Base branch: `main`.
- Use `feature/<slug>` for managed integration branches,
  `task/<slug>/<work-package>` for task worktree branches, and `fix/<slug>` for
  small fixes.
- Use Conventional Commits with meaningful scopes, for example
  `feat(discrete-state): add versioned state ownership` or
  `test(phase-graph): cover invalid interface orientation`.
- WIP commits are allowed only on task branches. The final local squash commit
  must follow Conventional Commits and summarize scientific rationale and
  verification evidence in its body.
- Preserve the integration branch and task history until the user accepts the
  squashed branch.
- Never push, create a remote branch, open or modify a pull request, deploy an
  artifact, or trigger a remote workflow. The user owns remote operations.

## Change and generated-file policy

- Pre-release API and ABI changes do not require compatibility layers, but
  they must leave the repository internally consistent and documented.
- Preserve precision, tolerances, stopping criteria, determinism, and compiler
  floating-point behavior unless a change is scientifically justified and
  approved.
- Dependency changes require approval and coordinated updates to version pins,
  checksums, `docs/package-lock.json`, README instructions, and CI.
- Do not hand-edit generated build files, `compile_commands.json`,
  `.dependencies/`, Doxygen XML, `docs/node_modules/`, or `docs/dist/`.
- Commands that modify external or tracked state include dependency
  installation, CMake configuration, builds, `npm ci`, documentation
  generation, `./format.sh`, and the formatting workflow's commit and push
  steps.
