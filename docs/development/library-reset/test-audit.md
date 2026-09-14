# Independent test adequacy audit: version-only reset

Status: complete; no unresolved blocking findings. Audited 2026-09-14.

## Requirements and independent oracles

The approved retained API is `rift::Version` with public integer components and
`rift::current_version() noexcept`, returning semantic version 0.1.0. The reset
removes foundation APIs and tutorials, preserves development infrastructure, and
uses Catch2 with one executable/CTest per `*_test.cpp`.

Before inspecting `src/rift.cpp`, the skeptic derived these requirements from
`include/rift/version.hpp`, the approved plan, and project version:

- Each returned component must equal its independently specified literal: 0, 1,
  and 0. Wrong values, negative values, and component reordering that changes the
  tuple must fail. Swapping the equal major/patch values is an equivalent mutant.
- The call expression must remain `noexcept`; a compile-time assertion can
  constrain this contract. No runtime failure input exists.
- The returned object is a value with public integer components. Caller-created
  negative `Version` values are outside the return-value contract; there is no
  constructor validation requirement.
- Coverage must contain first-party executable lines and functions, with zero
  missed lines/functions/branches. Zero branches is a legitimate denominator,
  reported as not applicable. Empty reports cannot establish coverage.
- Historical coverage exclusions must disappear. Third-party Catch2 and the
  removed foundation implementation must not enter the active library scope.

The initial test independently checked all three version values but omitted the
`noexcept` contract. This gap was reported to the manager before implementation
inspection. Broad GCC coverage exclusion flags were also flagged for removal.

## Tool preflight

Read `AGENTS.md`, root/test CMake files, presets, coverage script, and CI workflow.
Available tools: CMake/CTest 3.28.3, Clang/LLVM 22.1.8, GCC/gcov 13.3.0,
project `.venv/bin/gcovr` 8.6, jq 1.7, and clang-tidy 22.1.8. System gcovr 7.0
also exists but is not the approved project tool. Catch2 is pinned to 3.16.0.

No repository-approved property, coverage-guided fuzz, or mutation integration
was found. `mull-runner`, `mull-runner-22`, and `afl-fuzz` are unavailable.
There is no input space to generate, shrink, or fuzz for the constant metadata
API; these categories are not acceptance gaps for this reset. Bounded temporary
fault probes are proportionate. No dependencies were installed or toolchains
changed by the skeptic. Reevaluate property generation and compiler-compatible
mutation/fuzz tooling when an input-bearing algorithm is approved.

## White-box review and results

The implementation is one unconditional aggregate return. No inputs, allocation,
mutable state, test-runner detection, special fixture paths, or numerical
algorithm exist. The literal test oracle originates from the approved project
version, not an implementation helper. A runtime metadata implementation is
necessarily constant; this is required behavior, not fixture overfitting.

The implementation owner added `static_assert(noexcept(rift::current_version()))`
after the initial critique. No other permanent tests were added: the three
component checks plus signature assertion constrain the complete current API.
The skeptic did not edit production or repository tests.

Serial isolated probes used unchanged `tests/version_test.cpp`, temporary source
or header copies, Clang 22.1.8, C++23, `-O2 -DNDEBUG`, and the already built
Release Catch2 3.16.0 archives. Tracked source/header/test SHA-256 digests were
identical before and after. No production build or coverage profiles were
modified. Tests used Catch2 seed 12345; there is no generated corpus.

| Probe | Independent expected result | Observed |
|---|---|---|
| Original version implementation | Three assertions pass | Exit 0 |
| Major 0 becomes 1 | Major assertion fails | Killed, exit 42 |
| Minor 1 becomes 0 | Minor assertion fails | Killed, exit 42 |
| Patch 0 becomes 1 | Patch assertion fails | Killed, exit 42 |
| Minor 1 becomes -1 | Minor assertion fails | Killed, exit 42 |
| Public declaration loses `noexcept` | Static assertion fails to compile | Killed, exit 1 |

All five selected non-equivalent faults were detected. This is a bounded manual
fault check, not an exhaustive mutation score. Each single-field substitution
is already a minimal counterexample, preserved in ignored evidence. Equal
major/patch exchange is equivalent for the specified version.

`ctest --preset release -N` reports exactly one test, `version_test`.
`build/release/tests/version_test --list-tests` reports one Catch2 test case.
`nm -C --defined-only build/release/librift.a` reports only
`rift::current_version()`. File enumeration shows only `include/rift/version.hpp`,
`src/rift.cpp`, `tests/version_test.cpp`, and test CMake configuration in these
active source directories. Test CMake uses nonrecursive `*_test.cpp` discovery,
links ordinary tests to `Catch2::Catch2WithMain`, and retains MPI registration
without any active MPI tests.

## Coverage and exclusion audit

Inspected actual GCC raw/adjusted and Clang reports: each has 1/1 executable
lines and 1/1 functions (100 percent); there are zero branches. Raw and adjusted
metrics coincide. No old Clang location allowlist or missed-count allowance,
source exclusion markers, or broad GCC `--exclude-*` options remain in the local
script or CI. There are no approved exclusions to adjudicate.

The synthetic gate harness extracts the actual Clang jq predicate and GCC
Python nonempty guard from `scripts/run_coverage.sh`. It passes trace fixtures
to the real project gcovr 8.6 with all three configured 100-percent thresholds.
Both local/CI command definitions were inspected for these thresholds and for
absence of exclusion flags; the remote workflow was not executed.

| Coverage fixture | Clang gate | GCC thresholds plus guard |
|---|---|---|
| One covered line/function; zero branches | Accept | Accept |
| Uncovered line | Reject | Reject (exit 2) |
| Uncovered function | Reject | Reject (exit 16) |
| Uncovered branch | Reject | Reject (exit 4) |
| Empty report | Reject | Reject |
| Empty line denominator | Reject | Reject |
| Empty function denominator | Reject | Reject |

The empty-function case is significant: gcovr alone returns success, and the
added nonempty guard rejects it. The branch fixture is accepted as valid gcovr
input and rejected for insufficient coverage, not because it is malformed.

## Reproducibility

Run from the repository root. All evidence is under the ignored
`build/library-reset/skeptic-*` paths:

```console
.venv/bin/gcovr --root . --filter 'include/rift/' --filter 'src/' --json-pretty --output build/library-reset/skeptic-gcovr-base.json build/gcc-coverage
python3 build/library-reset/skeptic-gates.py
python3 build/library-reset/skeptic-mutants.py
ctest --preset release -N
build/release/tests/version_test --list-tests
nm -C --defined-only build/release/librift.a
```

`skeptic-gates-results.json` records all seven coverage fixtures and exit codes.
`skeptic-mutants-results.json` records every exact compile and test argument
array; `skeptic-mutants.py` recreates the temporary probes. Runtime commands use
`--reporter compact --rng-seed 12345`. The compile configuration includes
`-std=c++23 -DNDEBUG -O2`, repository public includes, Release Catch2 source and
generated include directories, and existing `libCatch2Main.a`/`libCatch2.a` with
`-pthread`. Individual `.log` files preserve the failure diagnostics. No
installations, Git operations, or concurrent builds were performed.

## Ranked findings and residual risk

1. **Resolved test gap:** the original component checks did not protect the
   `noexcept` contract. The implementation owner's compile-time assertion now
   rejects the isolated signature fault.
2. **Resolved coverage policy gap:** blanket GCC exclusion options could hide
   uncovered branches without individual approval. They are removed from local
   and CI paths; the baseline has no exclusions.
3. **Nonblocking scope limitation:** generic MPI registration has no test source
   to exercise it after the reset. This is deliberate removal of foundation
   tests, not MPI behavior verification. Future input-bearing scientific APIs
   need their own independent oracles, property/fuzz strategy, and numerical V&V.

No production defect or surviving selected non-equivalent mutant was found.
Coverage proves execution of one function, not future algorithm adequacy.
Branch behavior, numerical accuracy, physical validation, uncertainty, and
performance claims are inapplicable to this metadata-only library. Existing
Clang coverage source enumeration is top-level; reassess it if future code is
organized into nested directories. Remote CI/Codecov/Pages execution remains
outside this local audit.
