# Rift contribution guidance

## Unit tests

- All C++ tests use Boost.UT. A fine-grained `expect()` assertion belongs to a
  named `_test`; related tests, including meaningful nested tests, belong to one
  `suite<"Primary public object or cohesive topic">`.
- No test or assertion may be registered in Boost.UT's global suite. The empty
  `Suite 'global' (0 asserts in 0 tests)` summary is expected, but every actual
  test belongs to its reviewed subject suite.
- Put ordinary test bodies in physical registration headers named
  `<subject>_<behavior>_<two-digit-sequence>.hpp`. Each starts with
  `#pragma once`, uses its unique `rift_test::<stem>` namespace (or
  `rift_test::mpi::<stem>` under `tests/mpi/`), defines inline helpers, and
  exposes `inline void register_tests()`. Do not use anonymous namespaces or
  declare suites in registration headers.
- Only reviewed `*_run_tests.cpp` subject runners may be executable C++ test
  sources. A runner physically includes all compatible registration headers
  for one public object or cohesive free-function topic. It creates exactly one
  automatic, captureless subject suite after the required MPI lifecycle setup,
  calls each selected registration function exactly once, and then calls
  `boost::ut::cfg<>.run()` once.
- Keep support/detail objects with their primary public object. For example,
  `SpaceDraft` belongs in `suite<"Space registry">`. Add a separate suite only
  for a materially separate public object or cohesive free-function topic.
- Dispatch every runner through an explicit, fail-closed mode before MPI
  initialization. Reuse the same subject binary through fresh serial, MPI-rank,
  lifecycle, and fatal CTest aliases when their registrations are compatible.
  Registration order must not affect behavior; combined subjects participate
  in the reverse-order fresh-process audit.
- Fatal or real-abort operations remain separate CTest aliases because the
  process exit is part of the oracle. Their selected Boost.UT test must still
  belong to the subject suite, while the external wrapper independently
  requires the exact Rift marker, native MPI-abort evidence, and expected
  status. Incompatible uninitialized/finalized lifecycle modes likewise use
  fresh process aliases of the relevant subject binary.
- The separately linked coverage-completeness runner is permitted because its
  whole-archive link contract is itself the oracle. New executable sources or
  selectors require an update to the reviewed runner/CTest golden matrix and
  layout verifier.
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

## Executable tutorials

- Keep the progressive user path in exactly numbered, standalone sources under
  `tutorials/`: `tutorial-001.cpp`, `tutorial-002.cpp`, and so on. Each source
  builds by default and must remain useful without including test helpers,
  `rift::detail`, or another tutorial source.
- Tutorials are MPI applications, not Boost.UT unit tests. Use `MPI_COMM_WORLD`,
  keep MPI RAII alive longer than every Rift object, and register each program
  as a two-rank CTest labeled both `tutorial` and `mpi` unless its lesson has a
  reviewed reason for another rank count.
- Use explicit non-`assert` runtime checks and a stable completion line so the
  executable remains a lightweight smoke test in Debug and Release builds.
- Narrative pages live under `docs/tutorials/`. Every displayed C++ block on a
  tutorial page must use a `<!-- rift:snippet tutorial-NNN.name -->` directive
  backed by the matching active region in the compiled source. Handwritten C++
  fences, unused regions, conditional regions, and duplicate directives fail
  the Sourcey build.
- Update `tutorials/tutorial_matrix.json` and the tutorial layout verifier when
  adding a tutorial. Tutorial CTests are deliberately outside the optimized
  unit-test subject-runner and LLVM executable manifests even though they carry
  the honest `mpi` label; the independent tutorial matrix guards their process
  and executable mapping.

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
  cmake --build --preset debug --parallel 6
  ctest --preset debug --output-on-failure
  ```

- This workstation is memory-limited during template-heavy Rift/deal.II
  builds. Six to eight concurrent build jobs are safe; use six by default.
  Never invoke an unbounded `--parallel`, and do not exceed eight jobs without
  the user's explicit approval.

- Replace `debug` with `release` for Release verification. Also run Release
  for public-interface, numerical, ownership, concurrency, or
  optimization-sensitive changes.
- Run clang-tidy during compilation with:

  ```console
  cmake --preset debug-tidy
  cmake --build --preset debug-tidy --parallel 6
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
- Serial tests run as single processes and generally use `MPI_COMM_SELF`.
- Put genuinely multi-rank registration headers in `tests/mpi/` and include
  them from their subject runner (or the cohesive MPI-infrastructure runner).
  Register each subject/rank process alias with `rift_add_mpi_alias()` and a
  reviewed selector. The helper uses CMake's MPI launcher variables; do not
  hard-code `mpirun` or `mpiexec`.
- Ordinary CTest granularity is one alias per subject/rank combination. Keep
  the historical source-level behavior names as exact Boost.UT `_test` names,
  not as separate CTest executables.
- Run all registered MPI tests with:

  ```console
  ctest --preset debug -L mpi --output-on-failure
  ```

  MPI launchers need permission to create local sockets. Containerized or
  sandboxed environments may require their normal MPI execution allowance.
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
- Generate raw Clang source-based line, function, and branch coverage with:

  ```console
  cmake --preset coverage-clang
  cmake --build --preset coverage-clang --parallel 6
  ./scripts/clang_source_coverage.sh
  ```

  The script uses `llvm-profdata`, `llvm-cov`, LCOV, and `genhtml`. It writes one
  raw profile per binary signature and process with LLVM's `%m` and `%p`
  substitutions. Every registered CTest invocation for a binary is run before
  that binary's raw profiles are merged and reported only against that exact
  executable.
  This pairing prevents ambiguous inline/COMDAT records from independently
  linked test executables. Before running tests, the helper verifies that each
  registered `serial`, `mpi`, or coverage-only CTest invokes exactly one
  discovered instrumented executable and that every discovered executable has
  at least one such CTest. Multiple rank-count CTests may map to one binary;
  coverage executes all aliases before one profile merge/export for that
  binary. The coverage-only completeness executable links `rift`
  as a whole archive, and its paired JSON export must contain every current
  `src/*.cpp` unit. It also explicitly instantiates the reviewed 2D/3D public
  template surface recorded in `tests/coverage/supported_templates.txt`; the
  guard rejects missing or extra manifest entries before merging. The helper
  fails if `llvm-cov` emits any diagnostic, validates each paired
  source-definition and branch total against LLVM's JSON summary, and then
  unions first-party LCOV data from `include/rift/` and `src/`. Raw line
  coverage counts distinct physical source lines; function coverage counts
  linked source definitions rather than template instantiations. The raw
  summary reports exact LLVM branch outcomes separately from the conservative
  LCOV-representable projection. The helper reparses its emitted metadata and
  requires LCOV's summary totals to match that projection exactly. LLVM 22 JSON
  identifies a branch's source through tuple element 6 (`FileID`) but does not
  export per-outcome folded flags. When the summary and zero counters identify
  a folded outcome uniquely, the helper retains LLVM's exact union and omits a
  partially folded decision only from LCOV, which cannot represent a lone
  outcome. If multiple folded identities remain possible, report generation
  fails instead of choosing the identity that maximizes apparent coverage. Each
  invocation creates a new `build/coverage-clang/coverage-profiles.*` directory
  and prints the exact raw summary and HTML-report paths. Override tool
  discovery with `LLVM_PROFDATA`, `LLVM_COV`, `LCOV`, or `GENHTML` only when
  using compatible tools.

  This guarded exact LLVM report is the authoritative 100-percent coverage
  gate. Its metrics are distinct physical source lines, canonical source
  definitions, and exact canonical authored branch outcomes. Authority is
  conditional on both completeness checks passing: the whole-archive guard
  prevents a current static-library production unit from escaping the linked
  denominator, and the reviewed template manifest covers the supported 2D/3D
  instantiations. LLVM still cannot prove coverage of an unsupported or
  omitted template instantiation. Report the conservative LCOV projection
  separately; it is a visualization/interchange projection, not the branch
  gate. After the completeness checks pass, the coverage command must exit
  nonzero when any authoritative covered count is smaller than its defined
  count; report-only 100-percent policy language is not an acceptable gate.

- Retain the separate raw gcov-compatible report for comparison with earlier
  evidence. Configure it independently; do not enable both coverage modes in
  one build:

  ```console
  cmake -S . -B build/coverage-gcov -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DBUILD_TESTING=ON \
    -DENABLE_SANITIZERS=OFF \
    -DENABLE_COVERAGE=ON \
    -DENABLE_LLVM_COVERAGE=OFF
  cmake --build build/coverage-gcov --parallel 6
  ctest --test-dir build/coverage-gcov --output-on-failure
  rift_root="$(pwd)"
  gcovr \
    --root "${rift_root}" \
    --filter "${rift_root}/include/" \
    --filter "${rift_root}/src/" \
    --exclude "${rift_root}/build/" \
    --gcov-executable "$(clang -print-prog-name=llvm-cov) gcov" \
    --exclude-pattern-prefix RIFT_RAW_NO_EXCLUSIONS \
    --print-summary \
    build/coverage-gcov
  ```

- The strict raw no-exclusion gcovr report is a mandatory published
  compiler-CFG diagnostic, but is not the coverage gate. Do not pass
  `--exclude-unreachable-branches`, `--exclude-throw-branches`, or gcov ignore
  options in the strict report, and do not add a fail-under threshold. Publish
  its raw line, function, and branch totals, missing locations, compiler, and
  gcovr versions alongside the authoritative LLVM metrics. Existing gcovr
  source markers are deliberately disabled by the alternate prefix above;
  no source exclusion or suppression is approved.
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
  find include src tests tutorials -type f \
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
