# Harden phase-graph and distributed discrete-state foundations

## Summary

This change turns Rift's phase-graph and discrete-state prototypes into a
collective, provenance-safe foundation for later sharp-interface solver work.
It adds explicit run/graph/mesh/space/state identities; deterministic graph
construction; immutable mesh snapshots; distributed adaptive phase support;
collective state transactions; bounded private-snapshot retention; and exact
binary64 regional-state synchronization. It also adds guarded source-based
coverage, true multi-rank tests, fatal-path process oracles, and a lower-cost
test-suite layout.

## Scientific and numerical rationale

- **Context of use:** pre-release infrastructure for associating later
  multiphase operators with the correct phase, mesh, finite-element space, and
  immutable state version.
- **Problem:** the starting serial/process-local prototypes could not support
  the accepted distributed/adaptive architecture without ambiguous provenance,
  unsafe communicator lifetimes, rank-local identity allocation, or incomplete
  hanging-face support.
- **Chosen method:** typed provenance and communicator-consistent identities;
  immutable ownership boundaries; owner-local support masks closed to a
  distributed least fixed point; owner-only active-FE selection with ghost
  synchronization; and transactional immutable state publication.
- **Key numerical semantics:** continuous Q1 phase fields on adaptive meshes,
  full-background level sets, component-compatible `FE_Nothing`, and exact
  object-representation comparison for regional and level-set binary64 state.
- **Limitations:** no conservation-law residual, interface reconstruction,
  complete PDE/ODE calculation, physical closure, or experimental prediction
  is implemented. Physical validation, solution verification, and uncertainty
  propagation are therefore not claimed.

The contract-level V&V campaign and its applicability limits are recorded in
[vv.md](vv.md).

## Architecture and compatibility

- **Selected design:** incremental typed hardening rather than a monolithic run
  bundle or a serial-only retreat. The accepted decisions are summarized in
  [plan.md](plan.md) and the repository architecture ledger.
- **Data flow:** `RunConfiguration` owns the run context; `PhaseGraph`,
  `MeshSnapshot`, `SpaceSnapshot`, `StateLayout`, and `StateStore` carry the
  corresponding provenance and retain required lifetimes. State transitions
  agree a fixed collective descriptor before validation or mutation.
- **Source API impact:** intentionally breaking under the pre-release policy.
  Callers migrate from bare/process-local identifiers, mutable triangulation
  ownership, per-field masks, and noncollective state mutation to the typed
  collective APIs. `StateSnapshotStamp` now has an explicit complete-identity
  constructor and cannot be default constructed; this enforces its existing
  provenance invariant instead of permitting an incomplete value.
- **ABI impact:** the static library and all consumers are rebuilt together;
  no stable ABI, install/export package, plugin boundary, or language binding
  exists yet.
- **Data/serialization impact:** `PhaseGraph::canonical_json()` remains an
  output-only schema. No restart/checkpoint compatibility promise is added.
- **Dependency impact:** no dependency was added or upgraded.

## Implementation

- Run-owned intracommunicator lifetime and globally collision-free in-process
  identifiers for runs, graph instances, mesh snapshots, space epochs, stores,
  transactions, state snapshots, publication epochs, and level-set revisions.
- Deterministic phase-graph validation, compatibility-callback ordering,
  orientation, canonical serialization, and RFC 3629 UTF-8 validation.
- Immutable 2D/3D mesh snapshots with duplicated communicator ownership,
  provenance, collective error agreement, and fatal MPI/allocation boundaries.
- One owner-local support specification per represented phase; distributed
  hanging-interface closure; analytic Q1-compatible `FE_Nothing` selection;
  ghost verification; and locally relevant hanging constraints.
- Move-only collective state authority with immutable snapshots, accepted-root
  lineage, atomic begin/seal/publish/discard, bounded transient/pinned private
  retention, and external-handle lifetime independent of lookup retention.
- Exact `std::uint64_t` transport of regional binary64 values, including signed
  zeros, subnormals, infinities, quiet/signaling NaNs, sign, and payload bits.
- Guarded exact LLVM coverage with a whole-archive production-unit guard and a
  reviewed supported-template manifest.
- Test-build convention and implementation that reuse coherent Boost.UT
  `suite<"meaningful name">` translation units and binaries where process
  isolation is not part of the oracle.

## Tests and adversarial audit

Development followed focused compile/test reds before production changes and
independent skeptic review after each T0--T7 task. The retained tests include:

- exact graph permutation, orientation, callback, UTF-8, provenance, and error
  precedence oracles;
- serial, two-rank, and three-rank immutable-mesh lifetime and communicator
  tests;
- independent exact adaptive-cell masks, analytic 2D/3D DoF counts, nonvacuous
  hanging constraints, constant/coordinate reproduction, and multilevel
  closure across owners;
- deterministic state-machine/retention models, manual mutant-killing tests,
  no-identity-drift checks, lineage and lifetime tests;
- exact hexadecimal regional/level-set representation corpora; and
- stage-armed fatal children that require the expected real `MPI_Abort` marker
  rather than accepting any nonzero process result.

Independent adversarial review found and drove regressions for several real
defects, including communicator cleanup before fatal handling and regional
cache mutation before recoverable identity exhaustion. Both were fixed and
the final T7 skeptic verdict was PASS.

No property-based, fuzzing, or mutation framework is configured. Deterministic
enumeration, injected seams, exact independent oracles, and temporary manual
mutants provide current evidence; this remains a tooling gap.

## Coverage

The authoritative gate is exact LLVM source coverage; strict raw gcovr remains
a published non-gating compiler-CFG diagnostic. Independent test-layout and
final quality auditors reproduced the complete gate on the exact source
candidate.

| Metric | Authoritative exact LLVM | Approved exclusions | Requirement | Result |
|---|---:|---:|---:|---|
| Lines | 2878/2878 | 0 | 100% | PASS |
| Canonical source definitions | 351/351 | 0 | 100% | PASS |
| Authored branch outcomes | 919/919 | 0 | 100% | PASS |

### Coverage exclusions

No source exclusion, `NOLINT`, sanitizer suppression, or coverage marker
remains. The strict raw report is produced without unreachable/throw
heuristics, ignore-error options, or marker exclusions.

## Verification and validation

- **System integration:** Debug/Release serial and registered two-/three-rank
  MPI execution across graph, mesh, space, and state boundaries.
- **Code verification:** exact adaptive support masks, analytic Q1 DoF totals,
  and constant/coordinate-linear reproduction across nonempty hanging
  constraints.
- **Representation verification:** rank-independent exact binary64 state and
  exact level-set revision behavior.
- **Calculation/solution verification:** not applicable; no complete numerical
  calculation is produced.
- **Physical model validation:** not applicable; no physical prediction is
  made.
- **Uncertainty quantification:** not applicable to the exact/discrete
  contracts; finite fixture/configuration coverage remains a limitation.

The predeclared campaign passed C0--C7. Debug ASan/UBSan and optimized Release
each passed all 187 registered tests; the focused graph/mesh/space/state replay
passed 28/28 in each configuration. The strongest defensible conclusion is
that the foundations satisfy their contracts for the tested deterministic
2D/3D Q1 fixtures, exact binary64 representations, MPICH ranks, and compiler
configurations. This supports later solver development, not physical
prediction or decision-critical use. See [vv.md](vv.md) for commands,
observations, uncertainty, and extrapolation limits.

## Performance

The closing performance work targets developer build latency only; it does not
change solver/runtime kernels or numerical behavior. One clean Debug ASan build
was measured with `/usr/bin/time -v` for each layout at the user-approved
six-job limit. Consolidating the phase-graph validation cases and reusing seven
MPI binaries reduced C++ compile steps from 191 to 170 and executable links
from 186 to 165. Wall time fell from 10:51.67 (651.67 s) to 9:44.63
(584.63 s), a 67.04 s or 10.29% reduction (1.115x speedup). Maximum RSS was
effectively unchanged, 1,229,124 versus 1,228,780 KiB, and both builds reported
zero swaps. GNU time reports the maximum RSS of one process, not aggregate
six-job memory pressure, so aggregate OOM headroom was not measured. The
187-test candidate Debug suite passed in full. Because this is one clean
observation per layout rather than a repeated benchmark, the deterministic
21-step work reduction is stronger evidence than the precise wall-time
percentage; no variance or confidence interval is claimed.

## Quality gates

| Gate | Configuration | Result | Evidence |
|---|---|---|---|
| Debug tests and sanitizers | Debug, ASan/UBSan, six-job build | 187/187 PASS | `vv.md` |
| Release tests | `-O3 -march=native -DNDEBUG`, six-job build | 187/187 PASS | `vv.md` |
| Exact LLVM coverage | whole-archive + 2D/3D manifest | 2878/2878 lines; 351/351 definitions; 919/919 outcomes | `verification.md` and final audit |
| Strict raw gcovr diagnostic | Clang gcov compatibility, no exclusions | 2189/2396 lines; 501/645 functions; 1652/3528 compiler-CFG branches, published non-gating | `verification.md` |
| clang-tidy | clean whole first-party tree, Clang-Tidy 22.1.8 | zero diagnostics | final audit |
| Formatting/diff | clang-format dry-run and `git diff --check` | PASS | final audit |
| Documentation | Doxygen warnings-as-errors and Sourcey | PASS; 136 pages | final audit |
| Contract V&V | predeclared C0--C7 campaign | PASS | `vv.md` |
| Independent executable audit | exact source candidate `afe95dd` | all executable gates PASS | final audit |

## Local Git state

- **Base revision:** `268be4ba78cf61a87205f6636629eeae591ea6ee`
- **Integration branch:** `feature/phase-graph-discrete-state-refinement`
- **Executable-audited source revision:** `afe95dd49a898d8ac402146d4986c8e29dac6714`
- **Refreshed V&V evidence commit:** `e9c5910` (same audited source behavior;
  documentation-only provenance refresh)
- **PR-ready local branch:** `feature/phase-graph-discrete-state-refinement-pr`
- **Final squash commit:** this local squash commit; its exact SHA is reported
  in the handoff because a commit cannot embed its own identifier
- **Squashed-tree equivalence:** required and verified locally before handoff
- **Remote status:** not pushed; no pull request opened

## Reproduce

All builds are deliberately capped at six jobs on this workstation.

```sh
cmake --preset debug
cmake --build --preset debug --parallel 6
ctest --preset debug --output-on-failure

cmake --preset release
cmake --build --preset release --parallel 6
ctest --preset release --output-on-failure

cmake --preset coverage-clang
cmake --build --preset coverage-clang --parallel 6
./scripts/clang_source_coverage.sh

cmake --preset debug-tidy
cmake --build --preset debug-tidy --parallel 6

doxygen Doxyfile
npm ci --prefix docs
npm run --prefix docs build

find include src tests -type f \
  \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) \
  -exec clang-format --dry-run --Werror {} +
git diff --check 268be4ba78cf61a87205f6636629eeae591ea6ee..HEAD
```

The exact focused V&V commands and raw-coverage command are retained in
[vv.md](vv.md), [verification.md](verification.md), and `AGENTS.md`.

## Documentation

- User workflows: `docs/introduction.md`, `docs/spaces-and-support.md`, and
  `docs/collective-state.md`
- Architecture: `docs/architecture/01-phase-graph.md`,
  `docs/architecture/02-discrete-state.md`, and
  `docs/architecture/24-decision-status.md`
- Feature plan/evidence: [plan.md](plan.md),
  [verification.md](verification.md), [vv.md](vv.md), and the final performance
  report

## Risks, limitations, tooling gaps, and follow-up

- Tested MPI evidence uses MPICH/Hydra; the documented OpenMPI/GCC and broader
  compiler-by-MPI CI matrix remains to be implemented.
- No property/fuzz/mutation framework is configured.
- The exact LLVM template guard covers the reviewed supported 2D/3D surface,
  not arbitrary unsupported template instantiations.
- Raw Clang-gcov counters for a shared static library are final-writer unstable
  across many test executables; the strict raw diagnostic is published but is
  not the source-coverage gate.
- Full solver V&V, physical validation, UQ, scaling, and runtime-kernel
  performance are deferred to the solver features that make those claims.

## Reviewer guide

- [ ] Review the accepted contracts in `docs/architecture/01-phase-graph.md`,
  `docs/architecture/02-discrete-state.md`, and the decision ledger.
- [ ] Review collective provenance, communicator lifetime, and fatal/error
  boundaries in the public headers and `src/` implementations.
- [ ] Inspect the independent adaptive-mask, Q1-constraint, retention-model,
  and exact-bit test oracles.
- [ ] Confirm the coverage whole-archive and supported-template completeness
  guards before relying on 100% metrics.
- [ ] Reproduce the contract-level V&V campaign in [vv.md](vv.md).
- [ ] Review the measured test-build consolidation and confirm that isolated
  fatal/MPI process contracts remain separate.
- [ ] Inspect the final squashed diff against
  `268be4ba78cf61a87205f6636629eeae591ea6ee`.

## Subject-runner promotion addendum

This candidate also promotes the user-approved one-runner-per-subject
convention to the complete C++ fleet. Ten subject executables and one
MPI-infrastructure executable now serve 12 serial aliases, 11 ordinary
subject/rank aliases, and nine isolated fatal aliases. The whole-archive
coverage guard remains a twelfth, coverage-only executable. All behavior units
are physical `.hpp` registrations under meaningful subject suites; all 17
formerly manual MPI checks and all nine fatal setups are Boost.UT tests. The
empty global-suite summary contains zero tests and zero assertions.

The migration preserves 202 existing names, 1,113 existing expectation sites,
17 manual MPI oracles, nine fatal process cells, five production units, and 18
reviewed template uses. Additive naming yields 234 unique tests and 1,148
expectation sites. Golden layout/CTest matrices, fail-closed selector checks,
fresh reverse-order runs, verbose suite audits, native MPI-abort evidence, and
per-alias nonempty coverage-profile checks protect these contracts.
The committed legacy-name inventory requires each of those 202 names exactly
once while permitting the 32 reviewed additions; a rename mutant proves that
the gate is not count-only.

The final independent audit closes the remaining evidence gaps: all 166
source/header/suite/selector/rank/fatal mappings are now machine-enforced; all
eight combined subjects run in reverse registration order; rank-separated
verbose output proves exact global-suite zero counts without MPI stdout races;
the focused V&V selector rejects empty or stale matches; and the coverage guard
preprocesses the source, removes literals, and requires active
explicit-instantiation/`static_assert` structures that the coverage target then
compiles. Comment, string-literal, `#if 0`, and bare-name mutants cannot satisfy
its 18-entry manifest.

On the same Clang 22.1.8 workstation with exactly six build jobs, the exact
final clean Debug build fell from 607.77 s to 66.32 s (9.164x; 89.09%) and the
coverage build from 311.55 s to 31.82 s (9.791x; 89.79%). Debug per-process
maximum RSS increased 11.62% to 1,372,856 KiB and coverage RSS increased 8.34%
to 1,162,980 KiB; both reported zero swaps, while aggregate six-job memory was
not measured. The complete Debug suite passed 52/52, exact LLVM coverage
remained 2878/2878 lines, 351/351 definitions, and 919/919 authored outcomes,
and whole-tree clang-tidy completed with zero diagnostics. The detailed
benchmark, commands, uncertainty, strict raw diagnostic, and checksums are in
[performance.md](performance.md) and [verification.md](verification.md).

After adding the audit-only focused selector, the final affected Debug suite
passed 54/54 in 23.68 s and the exact 15-alias focused V&V replay passed 15/15
in 5.56 s. Exact coverage remained 2878/2878 lines, 351/351 definitions, and
919/919 authored outcomes.

The optimized Release build also passed 52/52 CTests after a clean six-job
build (35.86 s build, 19.04 s tests, 1,015,836 KiB per-process maximum RSS,
zero swaps). The audit-closure Release rerun passed 54/54 in 20.25 s, and its
exact focused replay passed 15/15 in 4.38 s. Doxygen remained warning-free and
Sourcey generated 136 pages.

Production API, ABI, numerical behavior, precision, tolerances, determinism,
and data formats are unchanged. Reviewers should focus on subject ownership,
selector/lifecycle dispatch, fatal external-oracle preservation, and the
source-to-registration-to-CTest golden matrix.

## Executable tutorial addendum

The candidate now includes a progressive public learning path:

1. Tutorial 1 creates an MPI run and immutable oriented phase graph.
2. Tutorial 2 adds a distributed deal.II mesh, phase-local fields, and a
   full-background level-set space.
3. Tutorial 3 allocates state, edits a private trial, seals it, and publishes
   accepted and previous immutable snapshots.

Each tutorial is a standalone two-rank executable built by default. Narrative
Sourcey pages import their C++ examples from strict marked regions in the
compiled source; handwritten duplicate C++ snippets are forbidden. A reviewed
matrix and tooling test enforce the exact three-file sequence, public-only API,
MPI-world RAII lifetime, CTest mapping, labels, process count, timeout, and
non-assert completion oracle. Contribution guidance, `AGENTS.md`, formatting,
navigation, README commands, and related conceptual guides document the
convention.

The final Debug and Release suites pass 58/58, the three tutorial smoke tests
pass on two ranks, Clang-Tidy and Doxygen are diagnostic-free, Sourcey builds
139 pages, and exact source coverage remains 100% at 2878/2878 lines, 351/351
definitions, and 919/919 authored branches. A clean Debug build with exactly
six jobs took 85.26 s and reported zero swaps. No production header or source
changed, so API, ABI, numerical semantics, precision, determinism, and data
formats are unchanged. Tutorial smoke coverage is kept separate from the
optimized unit-test coverage manifest by exact-label regression tests.

The tutorial campaign's strict raw gcovr JSON has SHA-256
`887c22c3aefd09fef1b583764585634af24b1fff355c329879d3a693e0410b38`.
Its committed [branch inventory](tutorial-raw-gcovr-branch-misses.tsv), [line
inventory](tutorial-raw-gcovr-line-misses.tsv), and [function
inventory](tutorial-raw-gcovr-function-misses.tsv) record 620 locations/1,320
uncovered compiler-CFG arcs, 34 uncovered lines, and 11 uncovered functions.
Their SHA-256 values are `ebf7b3a48db0f423035fe3110d20ea0e172307d1e4dd9aa8e9ec1f275f63ab1e`,
`a4b94bb8ad6f1e431500056acb914c7fa53a116d5bdda7752dcb091137136724`,
and `53880cd343ea4d9046225f14e5299c36ace77ecedcda6fbd1f80183b8f75b86a`,
respectively. This raw shared-`.gcda` diagnostic is non-gating; the exact
per-alias LLVM campaign remains authoritative.

The skeptic closure strengthened every smoke oracle. Tutorial 1 records and
collectively verifies the exact one-call/rank oriented compatibility callback.
Tutorial 2 independently verifies the `x = 0.5` center classification for every
owner-local cell, its exact phase partition, two globally nonempty phase
requests, request containment after support closure, and an empty regional
schema. Tutorial 3 compares every owner-local field entry and regional scalar by
binary representation in initial, accepted, and previous state, while also
checking publication epochs and level-set revisions. Intentional live mutants
failed their named two-rank CTests before restoration, including a swapped
liquid/gas classifier that the independent Tutorial 2 geometry oracle rejects.

The snippet parser now rejects all reviewed CommonMark backtick/tilde C++ fence
forms and any snippet directive embedded in a fenced block. The layout verifier
requires the exact configured MPI launcher, rank flag, and value two in each
CTest command; direct, wrong-launcher, wrong-flag, and one-rank mutants fail.
