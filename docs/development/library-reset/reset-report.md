# Reset Rift to a version-only library with Catch2

Rift's foundation implementation and its behavioral tests/tutorial are removed
so the library can be redesigned. The remaining public surface is the unchanged
`rift::Version` and `rift::current_version()` API reporting 0.1.0. The existing
build, scientific dependencies, CI, coverage, formatting, and Sourcey pipeline
remain in place.

The complete previous implementation is preserved at
`reference/pre-reset-status` (`400a7f054a79c46280c07d618005863218af3a4e`). Use
`git show reference/pre-reset-status:<path>` to consult it. Historical foundation
and PDE development plans remain explicitly marked as archived. They do not
prescribe the new architecture. Ignored planning and level-set research files
are preserved.

## Changes and compatibility

- Delete all foundation headers, implementation files, behavioral tests, and
  tutorial files. Keep `include/rift/version.hpp` and `src/rift.cpp` byte-for-byte
  unchanged.
- Preserve `rift::rift`, C++23, compiler presets, sanitizers, warnings, and
  dependency installation/cache workflows.
- Replace Boost.UT with Catch2 3.16.0, pinned to upstream commit
  `317ac1ed4c0bb6e6b91eafc817e05c488feffcb3`. Ordinary `*_test.cpp` sources link
  `Catch2::Catch2WithMain` and retain one same-named CTest per executable.
- Preserve generic one-, two-, and three-rank MPI registration. Future MPI tests
  supply an MPI-aware main and link `Catch2::Catch2`. No MPI test remains in
  this baseline.
- Remove historical Clang exclusions and broad GCC exclusion flags. Reject
  empty line/function coverage, while accepting and explicitly reporting a
  branch-free library.
- Exclude only the fetched Catch2 implementation targets from clang-tidy;
  continue checking Rift and its tests.
- Update current documentation and agent guidance. Preserve Sourcey
  configuration, style, package lock, Doxyfile, and documentation/Pages workflow.
  Clear stale generated Doxygen XML before rebuilding the site.

This intentionally removes the old source API and ABI; pre-release repository
policy does not protect either. No retained version behavior changes. No new
serialization/data format or replacement scientific API is introduced. The
scientific dependency stack is unchanged; only the unit-test framework changes.
See [approved scope and decisions](implementation-plan.md).

## Verification evidence

| Gate | Result | Evidence under `build/library-reset/` |
|---|---|---|
| Debug build and test | PASS; one CTest, three runtime assertions plus noexcept compile check | `debug-build.log`, `debug-test.log` |
| Release build and test | PASS; same assertions active in Release | `release-build.log`, `release-test.log` |
| GCC coverage | PASS; raw and adjusted metrics identical | `gcc-coverage.log` |
| Clang coverage | PASS; no exclusions | `clang-coverage.log` |
| ASan/UBSan | PASS in Debug with configured sanitizers unchanged | `debug-test.log` |
| clang-tidy | PASS with four nonblocking Catch2 macro warnings | `debug-tidy-build.log`, `debug-tidy-test.log` |
| Doxygen and Sourcey | PASS; six pages and only retained API in generated XML | `docs.log` |
| Generated API links | FAIL; inherited Sourcey pretty-URL issue, also reproduced on backup tag | `audit-baseline-docs-results.json`, `audit-static-results.json` |
| Independent test review | PASS; five selected faults detected, seven coverage fixtures checked | [Test audit](test-audit.md) |
| Final independent quality audit | READY FOR HUMAN REVIEW; no blocking reset findings | [Quality audit](quality-audit.md) |

The focused test-first build failed on the missing Catch2 header before the
framework migration (`red-version-test.log`). The final test uses independent
literal expected version components and a compile-time noexcept assertion.
Coverage is evidence of exercised code, not of oracle strength; adversarial
checks are recorded separately in the test audit.

The skeptic ran four independent wrong-component/value mutants in optimized
Release-mode test copies and verified that removing `noexcept` fails the static
assertion. All selected faults were detected; this is a bounded check, not an
exhaustive mutation score. Seven synthetic coverage fixtures confirmed correct
acceptance/rejection of valid zero-branch, incomplete, and empty reports. Test
seed 12345 and exact compiler/runner arguments are recorded in the audit evidence.

Debug's first restricted execution encountered LeakSanitizer's ptrace limitation.
The same test passed outside that restriction with no sanitizer suppression or
runtime-behavior change. Clang-tidy's remaining diagnostics originate from
Catch2's `TEST_CASE` generated static function (`misc-use-anonymous-namespace`)
and three `CHECK` do-while expansions (`cppcoreguidelines-avoid-do-while`). There
are no production diagnostics. The repository does not treat tidy warnings as
errors; these diagnostics remain visible.

The independent documentation link check found 15 missing-target occurrences:
generated API hrefs end in `.html`, while the unchanged `prettyUrls: "slash"`
configuration emits directory indexes. Rebuilding the tagged pre-reset source
and documentation with the same Sourcey installation reproduces the same broken
`core.html` and `rift-Version.html` links (and many other old API links). This is
an inherited documentation-tool issue, not a reset regression. The requested
Sourcey configuration and workflow are preserved; builds pass, but complete
generated-link correctness is not claimed. The independent quality audit records
the baseline comparison and treats this as a nonblocking inherited limitation.

## Coverage

| Compiler | Metric | Raw | Adjusted | Result |
|---|---|---|---|---|
| GCC 13.3.0 | Lines | 1/1, 100% | 1/1, 100% | PASS |
| GCC 13.3.0 | Functions | 1/1, 100% | 1/1, 100% | PASS |
| GCC 13.3.0 | Branches | 0/0 | 0/0 | NOT APPLICABLE |
| Clang 22.1.8 | Lines | 1/1, 100% | 1/1, 100% | PASS |
| Clang 22.1.8 | Functions | 1/1, 100% | 1/1, 100% | PASS |
| Clang 22.1.8 | Branches | 0/0 | 0/0 | NOT APPLICABLE |

No coverage exclusions are approved or applied. Both native tools encode the
empty branch denominator as zero percent; the wrapper explicitly prints that
there are no branches. It does not claim branches were exercised.

Raw GCC metrics: `build/gcc-coverage/coverage-raw-summary.json`. Adjusted GCC
metrics and Codecov input: `coverage-summary.json` and `coverage.xml` in the
same directory. Raw Clang metrics: `build/clang-coverage/coverage-summary.json`,
with detailed `coverage.json` and `coverage.lcov` alongside it.

## Scientific scope and limitations

This change introduces no numerical method, solver, physical model, scientific
data transformation, or calculation. Numerical code verification, solution
verification, model validation, uncertainty quantification, and performance
benchmarks are NOT APPLICABLE. Software/infrastructure verification supports
only the small retained API and development baseline; it says nothing about
future scientific functionality. ABI compatibility checking is NOT APPLICABLE
to this approved pre-release reset.

Full property/fuzz/mutation frameworks are not configured. A constant metadata
API has no input domain to fuzz; bounded isolated fault probes are used instead.
No new tooling installation is required for this scope. Multi-rank runtime
behavior is not claimed because the baseline has no MPI-dependent API/tests.
Remote GitHub jobs, Codecov upload, and Pages deployment are NOT RUN locally;
the existing workflow paths are preserved for the user's subsequent remote run.

## Reproduction

Tool versions: CMake 3.28.3, Clang/clang-tidy/LLVM/clang-format 22.1.8, GCC 13.3.0,
gcovr 8.6, Doxygen 1.9.8, Node 24.19.0, npm 11.17.0, Sourcey 3.6.5. Complete
version output is in `build/library-reset/tool-versions.txt`.

```sh
cmake --preset debug
cmake --build --preset debug --parallel 6
ctest --preset debug --output-on-failure
cmake --preset release
cmake --build --preset release --parallel 6
ctest --preset release --output-on-failure
./scripts/run_coverage.sh gcc
./scripts/run_coverage.sh clang
cmake --preset debug-tidy
cmake --build --preset debug-tidy --parallel 6
ctest --preset debug-tidy --output-on-failure
./format.sh
git diff --check
cmake -E make_directory build/doxygen
doxygen Doxyfile
npm ci --prefix docs
npm run --prefix docs build
```

The reset verification removed only obsolete generated coverage object/profile
files and old generated Doxygen XML before these builds, so removed source did
not pollute reports. Dependency installations and user-authored files were not
cleaned. Existing local Clang dependency builds serve Debug/Release and the
isolated GCC stack under `.dependencies-gcc/` serves GCC coverage.

## Local Git and reviewer guidance

The user's `library-reset` branch serves as both the working and review branch.
The approved delivery is one local commit above the backup revision; there are
no intermediate implementation commits to squash. The commit containing this
report is the reset commit (`git log -1 --format='%H %s'`). No additional branch,
push, pull request, or artifact publication is part of this task.

The audited source/configuration/documentation tree (excluding this report
directory) has manifest SHA-256
`d0a2231757676c1e4838695af627181893730186a843f7e5e294699b06f3bb10` in
`build/library-reset/audit-tree-sha256.json`. The manager verifies that manifest
again before the commit and checks that the commit tree matches the staged,
reviewed tree afterward.

- Review the intentional public API removal against the backup tag.
- Review the small Catch2 test and the empty-coverage rejection logic.
- Read the independent test and quality audits for evidence and limitations.
- Confirm future development uses the new scope rather than archived plans.
