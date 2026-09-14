# Independent quality audit: library reset

Audited 2026-09-14 on `library-reset`, against the approved reset in
[implementation-plan.md](implementation-plan.md). The auditor changed only
ignored build artifacts and this report; no source, test, configuration, or
user-documentation fixes were made. No dependencies were installed and no Git
state or remote operations were performed.

## Scope and reproducibility

Authoritative commands came from root `AGENTS.md`, `CMakePresets.json`, root and
test CMake files, `scripts/run_coverage.sh`, `Doxyfile`, `format.sh`, and all four
GitHub workflows. There were no additional ancestor or nested `AGENTS.md` files.
The accepted surface is unchanged `Version` / `current_version() noexcept`,
returning 0.1.0, with Catch2 replacing Boost.UT. No numerical implementation remains.

Commands below run from the repository root. `E` denotes the ignored evidence
directory `build/library-reset/`; report paths relative to a coverage directory
are written explicitly. JSON command records preserve argument arrays and exit
statuses. Builds used at most three jobs during this audit. Toolchain evidence
is `E/tool-versions.txt`: CMake/CTest 3.28.3, GCC/gcov 13.3.0, Clang/LLVM and
clang-tidy/clang-format 22.1.8, gcovr 8.6, Doxygen 1.9.8, Node 24.19.0, npm
11.17.0, and Sourcey 3.6.5. Presets and complete first-party compile commands
are recorded in `E/audit-compile-evidence.json`.

The tracked audited tree, including deleted paths, is fingerprinted in
`E/audit-tree-sha256.json` (SHA-256
`d0a2231757676c1e4838695af627181893730186a843f7e5e294699b06f3bb10`).
Manager/audit reports under this directory are excluded from that fingerprint.
The retained header and implementation are byte-identical to backup tag
`reference/pre-reset-status`, resolving to
`400a7f054a79c46280c07d618005863218af3a4e`.

## Evidence matrix

Each result applies to the named gate, not to untested future functionality.

| Gate | Command | Configuration | Result | Evidence path | Notes |
|---|---|---|---|---|---|
| Prerequisite check | `./scripts/install_dependencies.sh --check` | Existing local tools/dependencies | PASS | `E/audit-prerequisites.log` | Exit 0; no installation. |
| Debug configure/build | `cmake --preset debug`; `cmake --build --preset debug --parallel 3` | Clang Debug; ASan/UBSan | PASS | `E/audit-build-results.json`; `E/audit-debug-{configure,build}.log` | Both exit 0. |
| Debug full unit suite | `ctest --preset debug --output-on-failure` | Existing Debug binaries, outside ptrace sandbox | PASS | `E/audit-debug-test-unsandboxed.log` | Exit 0; 1/1 tests. |
| Focused unit test | `ctest --preset debug -R '^version_test$' --output-on-failure` | Debug, outside ptrace sandbox | PASS | `E/audit-focused-test.log` | Exit 0; focused test is also the entire current suite. |
| Release configure/build/tests | `cmake --preset release`; `cmake --build --preset release --parallel 3`; `ctest --preset release --output-on-failure` | Clang Release; `-O3 -march=native -DNDEBUG` | PASS | `E/audit-build-results.json`; `E/audit-release-{configure,build,test}.log` | All exit 0; 1/1 tests. Catch2 assertions remain active. |
| GCC coverage and compiler variant | `RIFT_COVERAGE_JOBS=3 ./scripts/run_coverage.sh gcc` | GCC Debug, isolated `.dependencies-gcc`, coverage enabled | PASS | `E/audit-gcc-coverage.log`; `E/audit-coverage-results.json`; `build/gcc-coverage/coverage*.json`; `build/gcc-coverage/coverage.xml` | Exit 0; 1/1 tests, raw and adjusted metrics below. |
| Clang coverage and compiler variant | `RIFT_COVERAGE_JOBS=3 ./scripts/run_coverage.sh clang` | Clang Debug, coverage enabled | PASS | `E/audit-clang-coverage.log`; `build/clang-coverage/coverage.json`; `build/clang-coverage/coverage-summary.json`; `build/clang-coverage/coverage.lcov` | Exit 0; 1/1 tests, raw and adjusted metrics below. |
| Coverage exclusion audit | Inspect coverage script, CI generation step, source markers, and raw/adjusted reports | `include/rift/` and `src/` | PASS | [test-audit.md](test-audit.md); coverage logs/reports above | No source exclusions, broad exclusion flags, or approved location allowlists remain. |
| Test discovery and Catch2 pin | `ctest --preset debug --show-only=json-v1`; compare fetched HEAD and `v3.16.0^{commit}` with test CMake | Ordinary `*_test.cpp`; retained future MPI registration | PASS | `E/audit-test-discovery.log`; `E/audit-preservation.json` | Exactly `version_test`; Catch2 commit `317ac1ed4c0bb6e6b91eafc817e05c488feffcb3` matches tag and pin. |
| Static analysis | `cmake --preset debug-tidy`; `cmake --build --preset debug-tidy --parallel 3`; `clang-tidy-22 -p build/debug-tidy src/rift.cpp tests/version_test.cpp` | Existing `.clang-tidy`, `WarningsAsErrors: ""` | PASS | `E/audit-tidy-results.json`; `E/audit-tidy-source.log` | All exit 0. Four visible macro-origin warnings assessed below. Direct analysis ensures an incremental build does not skip this gate. |
| ASan/UBSan, including normal leak detection | `ctest --preset debug --output-on-failure`; `ctest --preset debug-tidy --output-on-failure` | Both first-party translation units have `-fsanitize=address,undefined`; no ASAN/LSAN/UBSAN environment overrides | PASS | `E/audit-debug-test-unsandboxed.log`; `E/audit-tidy-test-unsandboxed.log`; `E/audit-compile-evidence.json` | Both exit 0 outside sandbox; no sanitizer findings. |
| TSan / MSan | No repository command or preset | No configured gate | NOT CONFIGURED | `CMakeLists.txt`; `CMakePresets.json` | Only ASan/UBSan are configured. |
| Formatting | `clang-format --dry-run --Werror include/rift/version.hpp src/rift.cpp tests/version_test.cpp`; `git diff --check` | Same source scope as `format.sh`, read-only equivalent | PASS | `E/audit-format.log`; `E/audit-whitespace.log` | Both exit 0; implementer already ran `./format.sh`. |
| Doxygen generation | `cmake -E make_directory build/doxygen`; `doxygen Doxyfile` | `WARN_AS_ERROR=FAIL_ON_WARNINGS` | PASS | `E/audit-doxygen.log`; `E/audit-docs-format-results.json` | Exit 0; XML index contains Version and no removed foundation API. |
| Sourcey dependency installation evidence | `npm ci --prefix docs` | Unchanged lockfile, Sourcey 3.6.5 | PASS | `E/docs.log` | Implementer evidence: exit 0, 255 packages installed. Reviewed rather than unnecessarily repeated. |
| Sourcey site build | `npm run --prefix docs build` | Unchanged configuration, style, package and lockfile | PASS | `E/audit-sourcey.log`; `E/audit-docs-format-results.json` | Exit 0; six content pages plus generated root redirect. |
| Generated internal HTML links | `python3 build/library-reset/audit-links.py docs/dist` | 111 local anchor links, including fragments | FAIL | `E/audit-current-links.json`; `E/audit-links.py` | Exit 1: 15 missing-target occurrences. Verified inherited Sourcey defect; see disposition below. |
| Baseline reproduction of documentation defect | `doxygen Doxyfile`; `npm run --prefix docs build` in isolated baseline directory; `python3 build/library-reset/audit-links.py build/library-reset/baseline-docs/docs/dist` | Tagged source/docs and unchanged installed Sourcey | PASS | `E/audit-baseline-docs-results.json`; `E/audit-baseline-links.json` | Baseline builds exit 0; link check exits 1 and reproduces the same Version/core URLs. This gate establishes provenance, not link correctness. |
| Backup, user planning, preserved infrastructure | `git show reference/pre-reset-status:<path>` byte comparison; SHA-256 comparison against initial manifest | Read-only checks | PASS | `E/audit-preservation.json`; `E/audit-static-results.json` | All 145 protected ignored planning files match. Sourcey configuration/style/lockfile, dependency installer, Doxyfile, formatting and docs/cache workflows match backup. |
| CI workflow integrity | Inspect diff against backup and current workflow definitions | GCC/Clang matrix; GCC thresholds and Codecov upload; docs/cache/format workflows | PASS | `.github/workflows/ci.yml`; `E/audit-preservation.json`; [test-audit.md](test-audit.md) | Only CI change removes coverage exclusions and enforces nonempty report denominators. General jobs preserved. |
| Hosted CI / Codecov / Pages / cache service | No remote execution authorized | GitHub-hosted environment | NOT RUN | Workflow definitions | Local compiler variants and documentation builds do not establish remote execution success. |
| Warning-as-error compiler gate | No repository command or preset | Existing `-Wall -Wextra -Wpedantic` retained | NOT CONFIGURED | `CMakeLists.txt`; `.clang-tidy` | Formatting and Doxygen have their own stricter error policies; compiler/tidy warnings are not errors. |
| Independent adequacy and bounded fault probes | `python3 build/library-reset/skeptic-mutants.py`; `python3 build/library-reset/skeptic-gates.py` | Skeptic's isolated Release probes, seed 12345 | PASS | [test-audit.md](test-audit.md); `E/skeptic-mutants-results.json`; `E/skeptic-gates-results.json` | Reviewed completed independent evidence: four runtime faults and noexcept signature fault detected; seven coverage fixtures behave correctly. |
| Approved property/fuzz/mutation integration | No repository command or approved integration | Constant metadata API with no input domain | NOT CONFIGURED | [test-audit.md](test-audit.md) | Manual bounded fault probes above are not an exhaustive mutation score. |
| MPI behavior/integration and tutorials | No active MPI test sources or tutorials remain | Generic CMake registration retained for future tests | NOT APPLICABLE | `tests/CMakeLists.txt`; `E/audit-preservation.json` | No MPI behavior verification is claimed. |
| Numerical V&V, model validation, uncertainty, performance | No numerical method/calculation in accepted scope | Version metadata only | NOT APPLICABLE | [implementation-plan.md](implementation-plan.md) | No scientific or performance claim requires experiments. |
| Source/ABI compatibility comparison | Repository explicitly waives these gates for pre-release reset | Approved removal of APIs | NOT APPLICABLE | `AGENTS.md`; accepted scope | Retained version header/source nevertheless unchanged. |
| Python pipeline and compiled documentation examples | No configured pipeline/example runner | Inline version snippets; no Python production code | NOT CONFIGURED | CMake, CI and Doxyfile | Coverage guard behavior is independently fault-tested; do not claim compiled snippet verification. |

## Coverage metrics and exclusions

| Compiler/report | Lines | Functions | Branches |
|---|---|---|---|
| GCC raw | 1/1, 100% | 1/1, 100% | 0/0, NOT APPLICABLE |
| GCC policy-adjusted | 1/1, 100% | 1/1, 100% | 0/0, NOT APPLICABLE |
| Clang raw | 1/1, 100% | 1/1, 100% | 0/0, NOT APPLICABLE |
| Clang policy-adjusted | 1/1, 100% | 1/1, 100% | 0/0, NOT APPLICABLE |

The sole executable source location is `src/rift.cpp:11`, function
`rift::current_version()`. The header contains declarations and documented data
members, with no executable function body. There are **no exclusions** requiring
unreachability arguments, suppressions, or reviewer approval. Third-party Catch2
is outside the explicitly defined first-party scope. Raw and adjusted GCC JSON
reports are identical; Clang has no adjustment and applies its gate to exported
raw totals. Tool JSON sometimes prints 0 percent for a zero denominator; the
scripts correctly explain that branches are not applicable. This is execution
evidence for one function, not evidence for future algorithms.

## Findings and disposition

1. **Inherited generated documentation links — FAIL, nonblocking for reset.**
   Missing targets include `../api/core.html` and `../api/rift-Version.html`
   (and the equivalent `../../` paths). The actual outputs are
   `api/core/index.html` and `api/rift-Version/index.html`. The unchanged Sourcey
   configuration uses `prettyUrls: "slash"`; some generated API links retain
   `.html`. No generic browser-side URL rewrite was found in emitted JavaScript.
   Independently rebuilding the backup produced the same Version/core links:
   34,853 missing-link occurrences among 38,421 checked links across 136 HTML
   files, versus 15 among 111 across seven current files. The standalone checker
   exits 1 for both; logs and exact target lists are preserved above. Likely
   owner: **docs / Sourcey dependency**. The manager explicitly classified this
   pre-existing issue outside the approved configuration-preserving reset;
   it is not silently converted to a pass. Fix in a separate documentation task.

2. **Sandbox LeakSanitizer restriction — resolved environment limitation.**
   Initial `ctest --preset debug --output-on-failure` exited 8 in the sandbox
   after `All tests passed (3 assertions in 1 test case)`, then reported
   `LeakSanitizer does not work under ptrace`. Evidence:
   `E/audit-debug-test.log`. Likely owner: **environment**. A narrowly approved
   outside-sandbox rerun of the exact test command exited 0. Sanitizer settings
   were not disabled or altered. The applicable sanitizer gate passes on the
   usable execution environment; the failed attempt remains recorded.

3. **Catch2 macro clang-tidy warnings — nonblocking.**
   Visible diagnostics are one `misc-use-anonymous-namespace` at
   `tests/version_test.cpp:6` and three `cppcoreguidelines-avoid-do-while` at
   lines 10–12. Expansion traces identify Catch2's generated static function
   and assertion wrapper. No first-party production diagnostic appeared.
   Existing `WarningsAsErrors` is empty, and direct analysis exits 0. The four
   framework-expansion style warnings do not indicate defects in the version
   test or justify a policy change. Likely owner if cleanup is desired:
   **tests / build tooling**. Third-party implementation targets skip Rift's
   tidy policy, while Rift and its test remain analyzed.

## TOOLING GAPS and practical limits

- No approved property/fuzz/mutation harness is configured; skeptic preflight
  found `mull-runner`, `mull-runner-22`, and `afl-fuzz` unavailable. None is needed
  for the current input-free constant API. Select compatible tools when an
  input-bearing scientific task is approved; nothing was installed here.
- No persistent generated-link gate exists in CI. The additional read-only
  audit probe exposed the inherited Sourcey defect above. External links and
  hosted routing were not tested.
- No TSan/MSan, compiler warnings-as-errors, Python production-pipeline, or
  compiled documentation-example gate is configured. These are recorded as
  absent, not passed. MPI registration has no live source to exercise today.
- Clang coverage currently enumerates top-level headers/sources, sufficient
  for this baseline. Revisit enumeration before adding nested source trees.
- Local logs and manifests are ignored build outputs. The tracked reports
  preserve results and reproduction commands; remote jobs remain user-owned.

All approved reset acceptance gates pass. The only unresolved failed check is
the explicitly documented inherited documentation-link defect outside this
reset's scope. The independent skeptic's final report has no blocking findings.

**READY FOR HUMAN REVIEW**
