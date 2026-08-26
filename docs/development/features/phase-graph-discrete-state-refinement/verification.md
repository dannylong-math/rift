# Verification record

## Intake baseline

Baseline tree: `268be4ba78cf61a87205f6636629eeae591ea6ee` plus no production
changes. The coverage data came from the existing clean Clang coverage build
in `build/coverage-tidy` after all 56 tests had passed.

Toolchain observed on 2026-08-22:

- gcovr 7.0;
- Clang 22.1.8;
- CMake 3.28.3;
- Ninja 1.11.1;
- MPICH/Hydra through the configured PETSc MPI wrapper;
- repository dependency check: usable.

The baseline report used first-party filters for `include/rift` and `src`,
LLVM gcov compatibility mode, unreachable-branch filtering, and gcovr's
duplicate-template function merge by maximum source line.

| Metric | Raw, no source exclusions | Existing-marker adjusted | Required |
|---|---:|---:|---:|
| Lines | 883/889 (99.3%) | 878/878 (100.0%) | 100% |
| Functions | 241/246 (98.0%) | 241/246 (98.0%) | 100% |
| Branches | 1002/1564 (64.1%) | 1002/1564 (64.1%) | 100% |

The existing `GCOVR_EXCL_LINE` markers are unregistered and therefore
`Proposed`, not approved. No policy-adjusted result will be accepted until
each requested exclusion has the evidence required by `AGENTS.md` and explicit
user approval. Function and branch coverage remain blocking gaps even when the
current line markers are honored.

## Baseline commands

```console
gcovr --root . --object-directory build/coverage-tidy \
  --gcov-executable 'llvm-cov-22 gcov' \
  --filter '(^|.*/)(include/rift|src)/' \
  --exclude-unreachable-branches \
  --merge-mode-functions merge-use-line-max \
  --print-summary --txt

gcovr --root . --object-directory build/coverage-tidy \
  --gcov-executable 'llvm-cov-22 gcov' \
  --filter '(^|.*/)(include/rift|src)/' \
  --exclude-unreachable-branches \
  --merge-mode-functions merge-use-line-max \
  --exclude-pattern-prefix RIFT_RAW_NO_EXCLUSIONS \
  --print-summary --txt
```

## Pending evidence

- scientific-method review;
- architecture conformance decision;
- reachable versus unreachable coverage classification;
- adversarial test findings and tooling gaps;
- 2D/3D and applicable MPI V&V;
- Debug/Release, static analysis, sanitizer, formatting, documentation, and
  final independent audit results.

## Scientific review synthesis

The phase graph is structurally conformant within its current first-stage
scope. Canonical identifiers, declared minus/plus orientation, unordered pair
lookup, compatibility argument orientation, simple-graph enforcement, and the
separation between permitted interactions and realized geometry agree with the
architecture. No governing equation or physical prediction is implemented at
this layer, so present evidence is software-contract verification rather than
solution verification or physical validation.

The highest phase-graph risks are boundary contracts: graph identifiers have
no provenance across graph instances; assignment can invalidate borrowed
descriptor views; stateful compatibility callbacks can make equivalent input
permutations differ; callback exceptions are undocumented; names and keys are
not validated as UTF-8; and the future level-set encoding has no explicit
orientation transform tying reconstructed normals to each graph edge. The
one-edge-per-named-phase-pair rule is correct but must be understood as one
global interface-law selection for that named pair.

The discrete-state implementation is broadly conformant for serial 2D and 3D
use, but current evidence does not support the accepted distributed/adaptive
claims. In particular, support-envelope validation and active-FE assignment do
not yet define safe rank-local versus global behavior for distributed
triangulations; no true multi-rank test exists; hanging-face conformity for
mixed real/`FE_Nothing` elements is neither tested nor guarded; a mutable
externally owned mesh can violate `SpaceEpoch` immutability; and communicator
ownership/congruence and collective-call requirements are unspecified.

Further state decisions concern value versus representation equality for
level-set revisions (`+0.0`, `-0.0`, and NaN), rank-local versus globally
consistent identity counters, transactions outliving their store, publication
from a stale accepted base, cell/mesh provenance, and how non-owning ranks read
rank-zero regional scalars.

The scientific reviewers recommend contract-level V&V using analytic DoF
counts, polynomial reproduction across hanging constraints, serial/two-rank
equivalence, partition-independent totals and revision transitions, graph
permutation invariance, and future manufactured interface-orientation tests.
Physical-data validation is not applicable to these two infrastructure stages.

## Architecture decision gate

Status: **Option 1 approved by the user on 2026-08-23. Concrete public API is
being specified for a separate review gate.**

The architecture review rejected declaring the current implementation complete
for the accepted distributed/adaptive contract. It considered three options:

1. Incrementally harden the existing boundaries with typed provenance,
   immutable mesh/communicator ownership, collective distributed envelopes,
   and safe state transitions.
2. Replace the current boundaries with one full immutable run bundle.
3. Narrow the second stage to serial execution and reverse the accepted MPI
   decision.

Option 1 is recommended. It is the smallest path that satisfies the accepted
architecture without prematurely coupling the phase graph, future geometry,
and solver lifecycle into one large bundle. Source/API changes are acceptable
under the pre-release static-library policy.

The user approved the complete recommended package without exceptions. The
semantic decisions below are therefore binding for the concrete API and
test-first task plan. Coverage exclusions were explicitly reserved for later
individual evidence and approval; none was approved by this decision.

During T1 adversarial review, an intercommunicator reproducer exposed divergent
and reused run identities, and disjoint intracommunicators exposed a possible
numeric collision. The user approved the focused resolution on 2026-08-23:
supported run communicators must be intracommunicators wholly derived from the
current `MPI_COMM_WORLD`; intercommunicators and unrelated dynamic/session
process worlds are rejected. Run identities are collision-free and never
reused within one MPI execution. Actual MPI-operation failures are fatal
rather than locally recoverable `expected` results.

The proposed semantic package is:

- explicit run/configuration provenance in graph, mesh, space, and state
  identities;
- nonassignable `PhaseGraph` values so borrowed descriptor views are not
  invalidated by in-place replacement;
- deterministic compatibility callbacks invoked in canonical interface order,
  with returned reasons treated as configuration errors and thrown exceptions
  propagated as registry/program failures;
- RFC 3629 UTF-8 validation without normalization or case folding;
- one composite interface-law key per named phase pair;
- replicated field schema plus owner-local support masks whose global union is
  validated collectively;
- registry-owned hanging-face conformity closure, owner-only active-FE
  assignment, and ghost synchronization through deal.II;
- immutable mesh snapshots with the communicator derived from and retained
  with the triangulation;
- collective distributed state transitions with communicator-consistent
  composite identities;
- bitwise level-set revision comparison, so signed zero differs while an
  unchanged NaN payload remains unchanged;
- weak transaction-to-store control, stale-base publication rejection, and
  bounded store retention while external immutable snapshots remain valid;
- a semantic regional-scalar API that gives every rank a synchronized value;
  and
- terminology that describes the current storage as a distributed vector
  bundle unless it is changed to deal.II's actual block-vector type.

The concrete multiphase level-set encoding, realized-adjacency tolerance,
junction operators, mesh-transfer transaction, physical validation, and
performance optimization remain later-stage work.

## Baseline adversarial audit

The skeptic confirmed one present contract defect: when interface
specifications exist but all fail structural resolution, omission of the
compatibility callback is not reported even though the architecture requires a
callback for every interface-bearing specification.

High-priority missing properties include graph and field permutation
invariance, exact callback arguments/count/order, multi-component partial
support, adaptive hanging constraints, rejected-draft epoch consumption,
invalid numeric IDs, snapshot lifetime, exact state values, NaN/signed-zero
revision behavior, and genuine two-rank ownership and reduction semantics.

The coverage tool also reports five uncalled functions even though four are
directly invoked and the fifth is an implicit destructor. Of 562 uncovered
branch arcs, 484 are mapped to source lines with no authored decision. This is
a gcov/COMDAT attribution problem in the current toolchain. The proposed
remedy is an additional Clang source-based coverage configuration using the
already installed `llvm-profdata-22` and `llvm-cov-22`; no dependency install
is needed. Raw gcovr metrics will still be retained.

All eleven existing `GCOVR_EXCL_LINE` markers remain unapproved. Five suppress
lines that are already covered and should be removed. The other six need
clean-build reproduction, adjacent tests, a `COV-###` register entry, and
explicit reviewer approval before they can affect adjusted metrics.

No property-testing, fuzz-target, or mutation-testing convention is currently
configured. Deterministic exhaustive and metamorphic tests can be written with
Boost.UT now. Clang's installed libFuzzer can be added later without a new
dependency; RapidCheck and Mull remain optional tooling proposals requiring
approval or compatibility evaluation.

## T0: MPI and source-coverage infrastructure

Task commit `e880447` was independently audited and merged into the integration
branch by `1545e0e`. The initial red oracle demonstrated that recursively
registered MPI source ran as one process and failed with `size=1` and rank sum
zero. The corrected registration uses CMake's MPI launcher variables, two
processes, an `mpi` label, a two-processor resource declaration, and a
60-second timeout. On the available MPICH/Hydra 5.0.0 installation, the focused
test passed and produced one raw profile for each of its two ranks.

The Clang 22 source-coverage path pairs every registered executable with only
the profiles produced by that executable. An authoritative CTest manifest is
checked against the executable set before reporting. The paired exports are
normalized into LCOV with canonical source-definition identities and exact
reconciliation among internal totals, emitted metadata, reparsed records, and
LCOV's summary. Four focused tooling tests cover line union, mapping-variant
function identity, branch source selection, and reconciliation failure.

The clean evidence build passed 61 of 61 tests: 56 serial C++ tests, one
two-rank MPI test, and four coverage-tooling tests. It emitted no compiler
warning or error diagnostics and no `llvm-cov` diagnostics. The reconciled raw
Clang metrics were:

| Metric | Covered / defined | Rate |
|---|---:|---:|
| Distinct physical lines | 715 / 717 | 99.7% |
| Linked source definitions | 93 / 93 | 100.0% |
| LCOV-representable branch outcomes | 214 / 220 | 97.3% |

The two missed lines are `discrete_state.hpp:994-995`. The six missed branch
outcomes are at `discrete_state.hpp:993`, `discrete_state.hpp:1060`,
`discrete_state.hpp:1209`, `discrete_state.cpp:183`, `phase_graph.cpp:555`, and
`phase_graph.cpp:565`. T0 intentionally records rather than manufactures
baseline completeness; later test-first tasks must close these reachable gaps.

At T0, LLVM's denominator contained only definitions linked by ordinary test
executables. T4 subsequently added the required whole-archive CTest and
reviewed supported-template manifest. Under the user-approved current policy,
the guarded exact LLVM distinct-line, canonical-definition, and canonical
authored-branch metrics are authoritative. Strict raw no-exclusion gcovr is a
mandatory published non-gating compiler-CFG diagnostic. No coverage exclusion
or diagnostic suppression was added. The available host proves MPICH/Hydra.
The OpenMPI/GCC leg remains an unexecuted capability and CI follow-up: as
stated in `AGENTS.md`, the staged compiler-by-MPI matrix is policy rather than
current repository capability until its CMake and CI jobs are implemented.

## T1: run and phase-graph provenance

Task commit `d13cbd6` was independently audited and merged by `e74e4be` after
the user approved the world-derived intracommunicator boundary. The first red
tests failed because `PhaseGraph` was assignable and the new run-configuration
header did not exist. Later adversarial red tests reproduced divergent and
reused IDs on an intercommunicator, collisions between disjoint communicators,
recoverable treatment of fatal MPI status failures, incomplete process-group
validation, and an unmatched collective cleanup before a rank-local fatal
allocation path.

The accepted implementation now provides:

- shared `RunConfiguration` control with an owned communicator duplicate;
- run IDs composed semantically from world origin and per-origin sequence,
  collision-free and never reused during one MPI execution;
- complete-member validation for supported world-derived intracommunicators;
- deterministic expected rejection of intercommunicators, unrelated process
  worlds, invalid lifecycle values, and finite ID exhaustion;
- immediate fatal handling of actual MPI status or rank-local allocation
  failures, without unmatched collective cleanup;
- communicator-consistent phase-graph instance IDs;
- `PhaseGraphProvenance` and checked `PhaseReference` boundaries;
- copy/move construction with deleted graph assignment; and
- shared-control lifetime after the caller releases its run handle and source
  communicator.

The final Debug ASan/UBSan suite passed 79 of 79 tests: 70 serial, five
two-rank MPI, and four tooling tests. The tested MPI was MPICH/Hydra 5.0.0.
Bounded Clang-Tidy 22 analysis of the 14 T1 translation units, full formatting,
compiler diagnostics, Doxygen, and a 93-page Sourcey build all passed.

Clang source coverage was complete for the new T1 implementation:

| Source | Lines | Functions | Branches |
|---|---:|---:|---:|
| `src/run_configuration.cpp` | 295 / 295 | 26 / 26 | 74 / 74 |
| `src/run_configuration_internal.hpp` | 10 / 10 | 6 / 6 | no mapped branches |

Whole linked first-party coverage was 1063/1065 lines (99.8%), 132/132
functions (100%), and 304/310 branch outcomes (98.1%). Every remaining miss
predated T1 in phase-graph or discrete-state code and remains assigned to T2
or later focused tasks. No exclusion or warning suppression was added.

## T2: deterministic collective phase graph

Task commit `6336584` was independently audited and merged by `a748f09`.
The first red regression demonstrated that an unresolved interface declaration
did not report `missing_compatibility_check`. The initial adversarial audit then
identified a collective exception-safety defect: after exact input agreement,
a rank-local allocation failure could unwind while peer ranks advanced to a
later callback collective. A focused private seam reproduced an allocation
failure after callback one and before callback two; before the seam existed,
the test failed to compile with the expected excess-argument diagnostic.

The accepted implementation now provides:

- exact byte-for-byte agreement on length-prefixed canonical graph input,
  while accepting declaration permutations without erasing multiplicity;
- canonical callback order and exact agreement on callback availability,
  outcome, and rejection reason;
- a collective callback exception boundary that rethrows each local original
  exception, including `std::bad_alloc`, and stops every rank before the next
  callback;
- fatal handling of internal post-agreement allocation failures without an
  unmatched cleanup or later collective;
- RFC 3629 UTF-8 validation, unsigned-byte ordering, byte preservation without
  normalization, and exhaustive JSON escaping for ASCII control bytes;
- the missing-callback diagnostic for every nonempty supplied interface list,
  including lists whose interfaces do not resolve structurally;
- canonical numeric lookup, one law per unordered phase pair, oriented edges,
  cycles, maximum IDs, and exhaustive three-phase declaration permutations;
  and
- public and private explanatory Doxygen, including the deterministic,
  side-effect-free callback precondition.

The skeptic required and verified explicit two- and three-rank disagreement
tests. The three-rank case changes only the middle rank, so an implementation
that compares endpoints but ignores an intermediate participant cannot pass.
The final UTF-8 oracle includes the minimized invalid boundaries `C1 80`,
`F0 8F BF BF`, and `C2 C0`, which respectively kill relaxation of the
two-byte lead lower bound, the four-byte scalar lower bound, and the
continuation-byte upper bound. The independent final T2 verdict was `PASS`.

The final Debug ASan/UBSan suite passed 101 of 101 tests: 87 serial, ten MPI,
and four tooling tests. The tested MPI was MPICH/Hydra 5.0.0. Changed-file
Clang-Tidy 22, full formatting, compiler diagnostics, Doxygen, and a 98-page
Sourcey build all passed.

Clang source coverage was complete for the T2 phase-graph implementation:

| Source | Lines | Functions | Branches |
|---|---:|---:|---:|
| `src/phase_graph.cpp` | 646 / 646 | 52 / 52 | 274 / 274 |

Whole linked first-party coverage was 1384/1386 lines (99.9%), 163/163
functions (100%), and 448/452 branch outcomes (99.1%). The remaining two lines
and four branch outcomes are the unchanged discrete-state baseline assigned to
T3--T7. No exclusion, diagnostic suppression, dependency, or numerical
floating-point behavior change was introduced. The exact final HTML report is
`build/coverage-clang/coverage-profiles.rbTXao/report/html/index.html` in the
T2 worktree.

## T3: immutable mesh ownership and provenance

Task commit `5f0898a` was independently audited and merged by `c1cf8fa`.
The first red ownership test failed because the approved `MeshSnapshot` API
did not exist. Later red tests established the required error vocabulary for
empty and unsupported triangulation kinds. The adversarial audit then found a
collective-cleanup defect: a rank-local distributed reconstruction failure
could unwind the only communicator control and enter `MPI_Comm_free` before
fatal handling while peer ranks continued through reconstruction. The focused
red regression initially failed to compile because no post-ownership builder,
allocator, or cleanup-control seams existed.

The accepted implementation now provides:

- additive `MeshSnapshotId`, `MeshSnapshotProvenance`, `MeshSnapshotError`,
  and `make_mesh_snapshot` APIs for 2D and 3D;
- collective null-first validation and deterministic logical-error precedence
  before dereferencing a rank-local triangulation;
- complete process-group translation into the current `MPI_COMM_WORLD`, with
  `MPI_IDENT` and `MPI_CONGRUENT` accepted and reordered or unequal
  communicators rejected;
- a documented collective precondition that every participant supplies a
  triangulation built on the same communicator context, because MPI cannot
  portably distinguish ranks alternating separate but congruent contexts;
- mesh identities composed from world origin and a process-global per-origin
  sequence, collision-free across disjoint and overlapping supported runs and
  never reused within one MPI execution;
- consumed triangulation ownership on every outcome, immutable public access,
  retained run control, and an owned authoritative mesh communicator;
- reconstruction of `parallel::distributed::Triangulation` on the owned
  communicator duplicate so snapshot lifetime does not depend on the caller's
  communicator handle;
- collective rejection of unsupported parallel triangulation kinds and empty
  distributed inputs; and
- immediate fatal handling for MPI, allocation, and reconstruction failures.
  Once communicator ownership has been acquired, rank-local fatal paths first
  abandon the shared communicator control and never enter collective cleanup;
  MPI process teardown owns reclamation.

The independent blocker regression covers allocation and non-allocation
reconstruction failures and final snapshot allocation failure in both 2D and
3D. Its fatal handler asserts that one duplication and zero communicator frees
have occurred. A separate three-member process-group oracle places
`MPI_UNDEFINED` only in the middle entry, so implementations that validate
only the first or last member cannot pass. The independent final T3 verdict
was `PASS`.

The final Debug ASan/UBSan and Release suites each passed 123 of 123 tests,
including 17 MPI tests. The tested MPI was MPICH/Hydra 5.0.0. Changed-file
Clang-Tidy 22, full formatting, compiler diagnostics, Doxygen, and a 111-page
Sourcey build all passed.

Clang source coverage was complete for the T3 implementation:

| Source | Lines | Functions | Branches |
|---|---:|---:|---:|
| `include/rift/mesh_snapshot.hpp` | 113 / 113 | 18 / 18 | 16 / 16 |
| `src/mesh_snapshot.cpp` | 271 / 271 | 23 / 23 | 72 / 72 |
| `src/mesh_snapshot_internal.hpp` | 6 / 6 | 4 / 4 | no mapped branches |

Whole linked first-party coverage was 1774/1776 lines (99.9%), 208/208
functions (100%), and 536/540 branch outcomes (99.3%). The remaining misses
are the unchanged discrete-state baseline: `discrete_state.hpp:994-995`,
branches at `discrete_state.hpp:993`, `:1060`, and `:1209`, and the branch at
`discrete_state.cpp:183`. No T3 exclusion, diagnostic suppression, dependency,
or numerical floating-point behavior change was introduced. The exact final
HTML report is
`build/coverage-clang/coverage-profiles.yL59nO/report/html/index.html` in the
T3 worktree.

## T4: distributed adaptive phase support

Task commit `eb3af62` was independently audited and merged by `4e01663`.
The independent final verdict was `PASS` after targeted red-green closure of
collective lifetime, cross-owner fixed-point, nonvacuous constraint,
diagnostic, coverage-enforcement, and fatal-oracle mutants.

The T4 implementation migrates phase fields to provenance-checked
`PhaseReference` values and one owner-local support record per represented
phase. `SpaceRegistry` retains an immutable mesh snapshot and collectively
reserves communicator-consistent, never-reused registry/epoch provenance.
Draft finalization is transactional through a non-const lvalue: logical errors
leave the draft active, while success alone publishes the immutable snapshot
and consumes finalization authority.

Distributed support closure publishes owner masks to ghosts, returns ghost
requests to owners, and iterates global changed agreement to the least fixed
point. Only owners receive active FE indices; deal.II synchronizes ghost
indices, which Rift verifies without querying artificial cells. The resulting
spaces use component-compatible `FE_Nothing`, owner/ghost-synchronized DoF
handlers, and closed locally relevant hanging constraints. Independent 2D/3D
serial and MPI oracles cover exact adaptive masks, a one-owner request that
closes onto cells owned by another rank, cascading multilevel closure requiring
more than one global changed iteration, scalar-Q1 DoF counts, nonempty hanging
constraints, constant and every coordinate-linear constraint reproduction,
multi-component disjoint support, independent canonical-schema and
phase-provenance divergences, provenance and support taxonomy, epoch
consumption, foreign-draft last-owner lifetime, communicator lifetime, and
fatal dependency boundaries. Two MPI fatal-path wrappers inject rank-asymmetric
allocation failure after agreement in `begin_draft` and `finalize`. Each outer
CTest passes only when its child exits nonzero and the injected retained fatal
handler emits the exact operation-specific `status=34` marker immediately
before real `MPI_Abort`; setup errors, launcher failures, timeouts, uncaught
exceptions, missing routing, and normal child exit fail the outer test.

The final Debug ASan/UBSan and Release suites each passed 156 of 156 tests: 116
serial, 27 MPI, and 13 coverage-tooling tests. The tested MPI was
MPICH/Hydra 5.0.0. Changed-file Clang-Tidy 22, full formatting, compiler
diagnostics, Doxygen, and a 120-page Sourcey build passed.

The coverage-only CTest links the complete static `rift` archive and explicitly
instantiates the reviewed 2D/3D public template surface. Before merging, the
guard checks all five current `src/*.cpp` units in its paired export and the
exact 18-entry template-use manifest. That guard initially exposed eleven
lines and five canonical definitions that the ordinary linked denominator had
missed; focused accessor checks closed them. Final authoritative exact LLVM
coverage is:

| Metric | Covered / defined | Rate |
|---|---:|---:|
| Distinct physical lines | 2356 / 2356 | 100.0% |
| Canonical source definitions | 280 / 280 | 100.0% |
| Exact canonical authored branch outcomes | 731 / 731 | 100.0% |

The conservative non-gating LCOV projection is 2356/2356 lines, 280/280
functions, and 730/730 branch outcomes. The strict raw no-exclusion gcovr
compiler-CFG diagnostic is 1948/1966 lines (99.1%), 541/543 functions (99.6%),
and 1978/3040 branch outcomes (65.1%). The exact 18 raw missing-line records
are `include/rift/discrete_state.hpp:1584,1690,1747,1802,2235,2458,2554`,
`include/rift/mesh_snapshot.hpp:405,548,666`,
`src/discrete_state.cpp:207,215,223,280`, `src/mesh_snapshot.cpp:97,395`, and
`src/phase_graph.cpp:881,1009`. The two raw missing function records are the
compiler-generated `MeshSnapshotError` copy constructor and the local reader
lambda instantiated inside `verify_synchronized_active_fe_index`. The exact
LLVM mapping records all authored lines, definitions, and branch outcomes as
executed, so these lower gcov records are published compiler-CFG/source-map
diagnostics, not gate adjustments. Raw branch misses include authored and
compiler-only exception, temporary, and COMDAT arcs; no unreachable/throw
heuristic was applied. All nine pre-existing unapproved exclusion markers were
disabled for the strict report; T4 adds no exclusion, suppression, or
dependency.

The strict report was generated with Clang 22.1.8, LLVM gcov compatibility
mode, and gcovr 7.0 using the exact no-exclusion command documented in
`AGENTS.md`, plus `--json
build/coverage-gcov-t4-audit/strict-no-exclusions.json`. Its SHA-256 is
`c3889a096fbba04cd69cd34f236403228476158e322c30cec9a63aa7c36bd54b`.
`python3 scripts/gcovr_miss_inventory.py --report
build/coverage-gcov-t4-audit/strict-no-exclusions.json --output-dir
docs/development/features/phase-graph-discrete-state-refinement` generated the
deterministically sorted [branch inventory](raw-gcovr-branch-misses.tsv),
[line inventory](raw-gcovr-line-misses.tsv), and
[function inventory](raw-gcovr-function-misses.tsv). They contain 559 branch
locations totaling all 1,062 uncovered compiler-CFG arcs, 18 line records, and
two function records. Their SHA-256 values are respectively
`1b3912095cc4ceecaad305534fddc56912017ab5b18cd5e695a74fb3c4102560`,
`098788d32158f3943c97a1a374ef764209fd5ea5a294023dcbe45fd76080e3f6`,
and `a2629cde8402b80455cecbc6ed29ef61cd00e8dbec85c61f72e94485cd05591e`.

The merger's authoritative fail-under behavior has independent negative tests
for an uncovered physical line, canonical definition, and exact canonical
authored branch outcome. Each fixture fails after the completeness guard; a
fully covered fixture passes. This prevents a future report-only regression
from printing policy language while returning success for an incomplete
authoritative metric.

No repository property-testing, fuzzing, or mutation-testing tool is currently
configured or available. Deterministic boundary, metamorphic, injected-failure,
and adversarial MPI tests provide the present oracles; the missing specialized
tools remain a named test-tooling gap for final reporting.

## T5: collective state protocol

Task commit `d8f1b8f` was independently audited and merged by `c559a9f`.
The independent final verdict was `PASS` after strengthening same-run
different-mesh agreement, private-base lineage, nonroot revision, global ID
union, tombstone, external-vector lifetime, rejected-transition identity, and
finite-state-model oracles.

T5 adds collectively created state stores, full run/graph/mesh/space/store
snapshot stamps, provenance-checked field references, immutable published
snapshots, weak-authority mutable transactions with retained collective
tombstones, and deterministic collective transition descriptors. Store,
transaction, snapshot, publication-epoch, and finite level-set revision
identities are reserved atomically and agree across the retained communicator;
logical rejection consumes no identity. Owner-local deal.II vectors use the
exact owned index sets and remain private until a successful collective
publication. T6 retention capacity and pinning, and T7 synchronized regional
reads plus bitwise signed-zero/NaN level-set semantics, remain explicitly
deferred.

The final Debug ASan/UBSan, Release, and raw-gcov builds each passed all 181
registered tests: 129 serial, 39 MPI, and 13 tooling tests. The exercised MPI
implementation was MPICH/Hydra 5.0.0. Rank-asymmetric fatal wrappers cover the
factory, dependency, begin, seal, and publish post-agreement boundaries with
stage-armed markers before real `MPI_Abort`; logical mismatch, exhaustion,
inactive, expired, stale, cross-store, and unknown-identity paths use fixed
structured errors and preserve the full pre-call state.

The final adversarial pass adds genuine two- and three-rank oracles for
run-first layout rejection across distinct congruent mesh contexts, nonroot
finite level-set revision changes, and the union of identities created on
world, reversed, overlapping, and disjoint self communicators. Serial and MPI
transition tests now exercise private-base root propagation, moved and sealed
tombstones, stale-candidate recovery, consecutive identities after every
logical rejection, store movement with a live transaction, and external vector
readability after discard, metadata eviction, and store destruction. A
deliberate immediate-private-root mutant failed
`state_store_private_lineage_00` before the production expression was restored.

The coverage completeness guard retained all five production translation
units and the reviewed 18-entry supported 2D/3D template-use manifest. The
authoritative exact LLVM fail-under gate reported:

| Metric | Covered / defined | Rate |
|---|---:|---:|
| Distinct physical lines | 2699 / 2699 | 100.0% |
| Canonical source definitions | 325 / 325 | 100.0% |
| Exact canonical authored branch outcomes | 845 / 845 | 100.0% |

The conservative non-gating LCOV projection is 2699/2699 lines, 325/325
functions, and 844/844 branch outcomes. The exact LLVM report is
`build/coverage-clang/coverage-profiles.VWm8cN/report/raw-summary.txt` in the
T5 worktree.

The mandatory strict raw no-exclusion gcovr compiler-CFG diagnostic was
generated independently with Clang 22.1.8, LLVM gcov compatibility mode, and
gcovr 7.0. It reports 2213/2237 lines (98.9%), 603/612 functions (98.5%), and
2165/3412 compiler-CFG branch outcomes (63.5%). No unreachable-branch,
throw-branch, ignore-error, source-exclusion, or fail-under option was used;
the alternate exclusion prefix disabled all source markers. These raw metrics
are published diagnostics and do not adjust the authoritative authored-code
gate.

The strict JSON command was the `AGENTS.md` workflow with
`--json build/coverage-gcov/strict-no-exclusions.json`; the report SHA-256 is
`36a878b1effb63a62b03269492dfe1b59f50507029142e599af4207c025e1af1`.
Running `python3 scripts/gcovr_miss_inventory.py --report
build/coverage-gcov/strict-no-exclusions.json --output-dir
docs/development/features/phase-graph-discrete-state-refinement` generated the
deterministically sorted [branch inventory](raw-gcovr-branch-misses.tsv),
[line inventory](raw-gcovr-line-misses.tsv), and
[function inventory](raw-gcovr-function-misses.tsv). They contain 613 branch
locations totaling all 1,247 uncovered compiler-CFG arcs, 24 line records,
and nine function records. Their SHA-256 values are respectively
`d8f792da594fce87f9f44cb8f25895e30c3cdf9ba7c01ffe35068f4c21080198`,
`a5ad16d428d39efdffd9a3b1257905cf693e5a805d986a1b2e41a690646892be`,
and `86607cc5d7deb122b5d03599ec034da93622e3edfd757efe290fb0f0d8a27f7d`.

## T6: bounded private state retention

Task commit `bb0d778` was independently audited and merged into the integration
branch by `e570df7`. The skeptic's final verdict was PASS after the active-base
capacity oracle was strengthened to pin a new transient while both transactions
retaining an evicted base were still alive.

The T6 task branch began at approved gate commit `3936bfb`. The first focused
red test, `state_store_retention_00`, failed to compile before production
changes with 13 errors across its 2D and 3D instantiations: `StateStore` had no
`pin_collective` or `unpin_collective` member and
`StateTransitionErrorCode` had no `pin_limit_reached` enumerator. The production
change was made only after this expected API red was recorded.

T6 implements the approved bounded lookup invariant: accepted, optional
previous, at most one unpinned transient private record, and at most the
configured number of explicitly pinned private records. The implementation
never evaluates `capacity + constant`, including when capacity is `SIZE_MAX`.
Pin, unpin, and discard toggle or erase retention metadata already stored in a
snapshot registry node after exact collective agreement; they allocate no node
and reserve none of the five finite identity components. Operation descriptor
values preserve the T5 values and append pin as 5 and unpin as 6.

The serial 2D/3D tests cover capacities 0, 1, 2, and `SIZE_MAX`; long repeated
sealing; duplicate pin and unpin; pin-limit atomicity; immediate unpin
promotion and exact former-transient eviction; transient-only discard;
publication of pinned and transient candidates; stale siblings that remain
registered; a second stale pinned sibling; active transactions whose base ID
is evicted; store movement; identity exhaustion; direct inspection of all five
identity sequences; and external immutable-handle lifetime after eviction,
discard, publication, and store destruction. Store-move coverage destroys the
moved-from store before exercising the destination's retention state. The
independent retention model uses symbolic insertion order, public results, and
a public lookup bitmap; it does not inspect or reproduce the production
registry encoding. It deterministically enumerates all 36 two-action suffixes
after an initial seal for every capacity/dimension pair, for 288 reproducible
short sequences whose expected result is computed before each product call.

The same MPI source is registered for true two- and three-rank execution. It
places the three-rank outlier in the middle and covers divergent arguments for
pin, unpin, and discard; pairwise operation collisions; divergent stores;
error precedence; zero transaction-ID drift; state atomicity after every
mismatch, including a pin mismatch that would mutate if executed before
agreement; cross-store checks of both stores after rejoining;
transient-sibling registration followed by stale publication; rank-one-only
external ownership after collective eviction; and active-base eviction.
Existing T5 lineage tests were updated intentionally to pin the first private
sibling where their purpose required two simultaneous private IDs.

The final skeptic closure also proves a first-time pin changes none of the five
identity sequences, failed seal exhaustion retains the former transient until
a successful retry evicts it, changed-level-set exhaustion has the same
atomicity, stale publication precedes epoch exhaustion, and multiple active
transactions retaining an evicted base consume no pin capacity: a new pin is
proved successful while both transactions are still alive and active. Four
temporary production mutants were run and then reverted: a no-op first pin, a
failed-seal transient eviction, pin mutation before collective agreement, and
a use-count-derived pin-capacity charge while active bases remain alive. Each
strengthened focused test failed at its intended assertion; the restored
production passed, so the closure exposed no production defect.

The final Debug ASan/UBSan, optimized Release, and raw-gcov diagnostic suites
each passed all 191 registered tests: 137 serial, 41 MPI process-weighted, and
13 coverage-tooling tests. Their final wall times were respectively 59.05 s,
52.12 s, and 51.77 s. The exercised MPI implementation was MPICH/Hydra 5.0.0.
The final compiler/tool observations were Clang and Clang-Tidy 22.1.8, CMake
3.28.3, Ninja 1.11.1, gcovr 7.0, Doxygen 1.9.8, and npm 11.17.0.

The completeness guard retained all five production translation units and the
reviewed 18-entry supported 2D/3D template-use manifest. The final
authoritative exact LLVM fail-under gate reported:

| Metric | Covered / defined | Rate |
|---|---:|---:|
| Distinct physical lines | 2770 / 2770 | 100.0% |
| Canonical source definitions | 330 / 330 | 100.0% |
| Exact canonical authored branch outcomes | 877 / 877 | 100.0% |

The conservative non-gating LCOV projection is 2770/2770 lines, 330/330
functions, and 876/876 branch outcomes. The exact LLVM report is
`build/coverage-clang/coverage-profiles.xDhGzB/report/raw-summary.txt` in the T6
worktree. No source exclusion, suppression, or dependency was added.

The final strict raw no-exclusion gcovr compiler-CFG diagnostic reports
2276/2300 lines (99.0%), 619/623 functions (99.4%), and 2198/3468 branch
outcomes (63.4%). It used Clang 22.1.8 LLVM gcov compatibility mode and gcovr
7.0 with the exact `AGENTS.md` filters and alternate marker prefix; no
unreachable-branch, throw-branch, ignore-error, source-exclusion, suppression,
or fail-under option was used. The strict JSON report SHA-256 is
`353477a40d30e61fe90bbcaf592de06fdf318c837b68012b46536d9a91b4127a`.

The deterministic [branch inventory](raw-gcovr-branch-misses.tsv),
[line inventory](raw-gcovr-line-misses.tsv), and
[function inventory](raw-gcovr-function-misses.tsv) contain 613 branch
locations totaling all 1270 uncovered compiler-CFG arcs, 24 line records, and
four function records. Their SHA-256 values are respectively
`47f2ecf0376037c560446811767611b6b95df8547600598c64185c68806e4d6e`,
`8d142914ddc3ea3ff1922eae84790879612c34fcb9c15923e4eb2585158d84d9`,
and `7e1eda147b1f936dcd79ba8916c5fb20f154c75e3917dd09ceb831c935ba5f9d`.

All 15 T6-owned or intentionally modified translation units pass Clang-Tidy
22 with zero diagnostics. A full-tree diagnostic attempt also exposed two
unchanged out-of-scope `misc-include-cleaner` warnings in
`tests/mesh_snapshot_world_members_00.cpp` and
`tests/phase_graph_reference_provenance_00.cpp`; they remain explicit residuals
for the final whole-tree quality audit and were not folded into the T6 edit
set. Full formatting and `git diff --check` passed. Doxygen emitted no warning,
and Sourcey generated 136 pages.

This task changes no numerical storage, precision, tolerance, collective
floating-point reduction, or T7 regional/bitwise behavior. It additively
extends the public source API and changes private static-library layout; Rift's
pre-release rebuild-together policy does not require ABI compatibility. No
property-based, fuzz, or mutation tool is configured, so deterministic model,
boundary, injected-exhaustion, and adversarial MPI tests provide the present
mutant-killing evidence.

## T7: exact regional state

Task commit `89e52eb` was independently audited and merged into the integration
branch by `35b6fad`. The skeptic's final verdict was PASS after a confirmed
seal atomicity defect was fixed with staged regional bits and the exact
post-agreement staging-allocation fatal boundary received a rank-asymmetric
real-abort regression.

The T7 task branch began at the approved implementation-gate commit `ec8c792`.
Before production changes, building the initial focused
`state_transaction_regional_00` test failed with 16 expected compiler errors
across the 2D and 3D instantiations because `StateSnapshot` had no
`regional_value` member and `MutableStateTransaction` had no
`set_regional_value_collective` member. Production implementation began only
after this API red was recorded.

T7 stores each regional degree of freedom as the exact IEEE-754 binary64 bit
pattern in a replicated cache and in a size-one backend vector owned by rank
zero of the state layout's communicator. The collective setter communicates
and compares `std::uint64_t` representations, then updates every cache and the
owner backend only after exact operation, store, transaction, entry, and value
agreement. It reserves none of the five finite identity components and leaves
both cache and backend unchanged on every rejection. Operation descriptor
values preserve all earlier values and append the regional setter as 7.

Sealing stages each owner-backend value with `MPI_UINT64_T` before any
level-set comparison or identity reservation, but does not commit those bits
to the transaction cache until every recoverable check and identity
reservation succeeds. Temporary allocation and broadcast failure remain under
the fatal state boundary. The exact post-agreement staging allocation has a
private injected factory seam; a two-rank child makes only rank one throw
`std::bad_alloc` while rank zero stages successfully and enters the real
broadcast, then observes the real abort marker
`RIFT_FATAL_ORACLE operation=state_regional_staging status=34`. Before the
production seam existed, building this child failed with the expected missing
`StateStoreAccess::seal_with_regional_staging` API and incompatible injected
factory errors. Level-set equality is now an
exact owner-local bit comparison followed by an exact boolean collective
reduction; it does not use floating-point numeric equality, NaN
canonicalization, or parity. A regional-only seal preserves the level-set
revision, while an exact level-set bit change advances it atomically. The new
post-agreement broadcast failure route uses the retained fatal-MPI mechanism;
its child test observes the real abort marker
`RIFT_FATAL_ORACLE operation=state_regional_sync status=15`.

The serial 2D/3D tests cover two-entry isolation, last-write-wins, immutable
base and sibling snapshots, local unknown-entry exceptions, setter precedence,
expired and inactive transactions, all-five identity sentinels, backend/cache
atomicity, retention and external-handle lifetime, and regional-only revision
preservation. Independent hexadecimal constants and `std::bit_cast` provide
the oracle for positive and negative zero, subnormals, finite values,
infinities, signed quiet-NaN payloads, and signaling-NaN payloads. Exact
level-set tests cover the same representation classes,
positive-to-negative infinity, same-payload NaN sign
changes, quiet-to-signaling NaN changes, identical signaling or negative
nonfinite bits, changed NaN payloads, and
atomic retry after revision exhaustion. A focused 2D/3D regression injects
owner-backend bits, exhausts only the snapshot sequence, and proves failed
sealing preserves cache, backend, active state, and all five identity counters
before a corrected retry synchronizes successfully. The test failed twice at
its unchanged-cache assertion against the original T7 implementation.

The MPI source is registered for true two- and three-rank execution and places
the three-rank outlier at rank one. It covers middle-rank entry and value
outliers, signed-zero mismatch, identical and changed NaN payloads, crossed
operations, stores, and transactions, full error precedence, rejection
atomicity, and zero drift in all five identity sequences. Divergent-entry
rejection inspects both entries on every rank and then completes a corrected
write and seal on the same transaction. The fatal synchronization child now
has only a nonroot rank return the injected error while its peer enters the
real broadcast, proving the retained abort route resolves the asymmetric
deadlock-sensitive failure before timeout. Getter values are
gathered independently from every rank. Direct backend mutation proves seal
synchronization refreshes all replicated caches, a last locally owned
level-set entry proves nonroot changes are observed, and simultaneous changes
on two ranks reject XOR/parity behavior. A reversed communicator separately
proves that backend ownership follows communicator rank zero, which is the
last world rank in that fixture, rather than `MPI_COMM_WORLD` rank zero.

The post-skeptic full Debug ASan/UBSan and optimized Release suites each passed
all 199 registered tests: 141 serial, 45 MPI process-weighted, and 13
coverage-tooling tests. Their wall times were 63.16 s and 54.15 s,
respectively. The final-source, target-clean raw-gcov diagnostic also passed
199/199 in 55.37 s and is recorded below. An earlier deterministic
serial-label raw replay passed 141/141 in 39.52 s while diagnosing the shared
gcda limitation. The
exercised MPI implementation was MPICH/Hydra 5.0.0. Tool observations were
Clang and Clang-Tidy 22.1.8, CMake 3.28.3, Ninja 1.11.1, gcovr 7.0, Doxygen
1.9.8, and npm 11.17.0.

The completeness guard retained all five production translation units and the
reviewed 18-entry supported 2D/3D template-use manifest. The final
authoritative exact LLVM fail-under gate reported:

| Metric | Covered / defined | Rate |
|---|---:|---:|
| Distinct physical lines | 2878 / 2878 | 100.0% |
| Canonical source definitions | 347 / 347 | 100.0% |
| Exact canonical authored branch outcomes | 919 / 919 | 100.0% |

The conservative non-gating LCOV projection is 2878/2878 lines, 347/347
functions, and 918/918 branch outcomes. The exact LLVM report is
`build/coverage-clang/coverage-profiles.14xNGD/report/raw-summary.txt` in the T7
worktree. No source exclusion, suppression, compiler floating-point option, or
dependency was added.

The final clean-cold strict raw no-exclusion gcovr compiler-CFG diagnostic
reports 2363/2393 lines (98.7%), 636/642 functions (99.1%), and 2243/3548
branch outcomes (63.2%). It used Clang 22.1.8 LLVM gcov compatibility mode and gcovr
7.0 with the exact `AGENTS.md` filters and alternate marker prefix; no
unreachable-branch, throw-branch, ignore-error, source-exclusion, suppression,
or fail-under option was used. The strict JSON report SHA-256 is
`0b5b16d929f89446588dd51c25eb1572219ea8154fffe098be8a4f68c5fa4c0e`.

The earlier T7 raw values of 98.8% lines, 99.1% functions, and 63.4% branches
remain superseded because they came from an incrementally accumulated profile.
An intermediate clean-cold build reported 81.3% lines, 82.6% functions, and
56.0% branches, whereas the final-source target-clean build above reported the
listed 98.7%, 99.1%, and 63.2%. Clang gcov compatibility mode's single shared
static-library gcda did not merge the per-executable and concurrent MPI
writers; whichever writer persisted last dominated the `discrete_state.cpp`
counters, and replaying every serial-label test during the earlier diagnostic
left that result unchanged.
This limitation is recorded rather than hidden or worked around inside T7.
The raw compiler-CFG report remains a mandatory non-gating diagnostic; the
paired per-process LLVM profile merge is the authoritative exact 100% gate.

The deterministic [branch inventory](raw-gcovr-branch-misses.tsv),
[line inventory](raw-gcovr-line-misses.tsv), and
[function inventory](raw-gcovr-function-misses.tsv) contain 623 branch
locations totaling all 1305 uncovered compiler-CFG arcs, 30 line records,
and 6 function records. Their SHA-256 values are respectively
`00f002d17d4c93ee9b3d9b21f24127430f6badd2ed6cc57b046c6753c54f4366`,
`4177d496ad112c2fbbb3410ff53821313b14a035edf8bae7d8ff5b8b1df1cfbb`,
and `cef9cdffa139034f243f3f1f89976e9379a1d02d2e70ddcc31fbd186f33f7b0f`.

All ten T7-owned or intentionally modified translation units pass
Clang-Tidy 22 with zero first-party diagnostics. Full formatting and
`git diff --check` passed. Doxygen emitted no warning, and Sourcey generated
136 pages.

This task additively changes the public source API and private static-library
layout; Rift's pre-release rebuild-together policy does not require ABI
compatibility. Its deliberate numerical change is exact representation-level
state comparison and synchronization for regional values and level sets:
signed zeros and distinct NaN representations are different, while identical
nonfinite representations are unchanged. No tolerance, solver arithmetic,
stopping criterion, or regional value is altered. No property-based, fuzz, or
mutation tool is configured; deterministic representation corpora,
injected-exhaustion tests, backend/cache inspection, and adversarial MPI tests
provide the present mutant-killing evidence. No physical-model V&V claim is
made because this task changes state transport and identity bookkeeping rather
than a model or numerical solution algorithm.

## Final integrated quality audit

The final integrated candidate `afe95dd49a898d8ac402146d4986c8e29dac6714`
includes the T0--T7 implementation, the test-build consolidation, direct-include
hygiene, removal of all source coverage markers, and the structural closure of
every whole-tree Clang-Tidy diagnostic and every feature-added `NOLINT` marker.
The final cleanup also makes `StateSnapshotStamp` explicitly and completely
constructible, but not default constructible, which enforces the already
approved full-provenance invariant. No precision, tolerance, stopping
criterion, floating-point compiler option, state representation, or solver
arithmetic changed during this cleanup.

An independent read-only quality campaign rebuilt and tested the exact clean
candidate sequentially with no more than six parallel build jobs. Debug
ASan/UBSan and optimized Release each passed all 187 registered tests in
56.78 s and 50.33 s. The raw-gcov tree also passed 187/187 in 51.73 s. A clean
whole-tree Clang-Tidy 22.1.8 build completed with zero first-party diagnostics;
the source tree contains no `NOLINT`, `GCOVR_EXCL`, `LCOV_EXCL`, or sanitizer
suppression marker. Doxygen emitted no warning, Sourcey generated 136 pages,
and formatting, Python/shell syntax, coverage-tooling tests, the executable
manifest, and `git diff --check` passed.

The whole-archive and supported-template completeness guard found all five
production translation units and all 18 reviewed 2D/3D template uses. Every
one of the seven shared MPI binaries executed both its base and three-rank
CTest aliases without profile collision. The authoritative exact LLVM gate
reported:

| Metric | Covered / defined | Rate |
|---|---:|---:|
| Distinct physical lines | 2878 / 2878 | 100.0% |
| Canonical source definitions | 351 / 351 | 100.0% |
| Exact canonical authored branch outcomes | 919 / 919 | 100.0% |

The conservative non-gating LCOV projection is 2878/2878 lines, 351/351
functions, and 918/918 branch outcomes. The report is
`build/coverage-clang/coverage-profiles.XklJ8G/report/raw-summary.txt` in the
integration worktree.

The mandatory strict raw no-exclusion Clang-gcov diagnostic reported
2189/2396 lines (91.4%), 501/645 functions (77.7%), and 1652/3528 compiler-CFG
branch outcomes (46.8%). Its JSON SHA-256 is
`9914d0607612800afd23a5f84d6312f6b0ceff20d5ccd4f07a52c48d048ec33a`.
These values are published as a non-gating compiler mapping diagnostic, not as
source-coverage evidence: the shared static-library `.gcda` files are
final-writer unstable across the many independent and MPI test processes.
This final observation therefore supersedes, but does not invalidate, the
different task-local raw observations recorded above. No heuristic branch
filter, ignore-error option, marker exclusion, or coverage suppression was
used.

The deterministic final [branch inventory](final-raw-gcovr-branch-misses.tsv),
[line inventory](final-raw-gcovr-line-misses.tsv), and
[function inventory](final-raw-gcovr-function-misses.tsv) contain 693 branch
locations totaling all 1876 uncovered compiler-CFG arcs, 207 line records,
and 144 function records. Their SHA-256 values are respectively
`93f4a57954fd136c396324dd0351cd99246a4a057acb7b1162d5c1faeb60d465`,
`d8dc7aaeb63841a7cad89028cd6ce73e146a5724ece681d1d846ee1dbc499af9`,
and `be777055e6b927b50b3c6c9e1a67220021b6cd26b8c1b43ad3e1a41091058c81`.

The exact executable evidence matrix is
`build/audit-afe95dd/final-quality-audit.md` with SHA-256
`9e266c010a5011149598850fd7d484b3c9949854e0be556456242fcbdede0a1e`.
The only environment/capability residual is the unconfigured OpenMPI/GCC CI
leg; MPICH/Hydra 5.0.0 was exercised through three ranks. No property-based,
fuzzing, or mutation framework is configured, so deterministic enumeration,
injected fault seams, independent analytic oracles, and temporary manual
mutants remain the available adversarial evidence.

## Full C++ subject-runner verification

The 2026-08-26 follow-up promoted the measured physical-header technique to
all C++ tests without changing production code. Ten subject runners, one
MPI-infrastructure runner, and the separately linked coverage-completeness
runner now own 166 uniquely namespaced registration headers. The structural
gate reports 234 globally unique named Boost.UT tests and 1,148 expectation
sites. All 17 former manual nonfatal MPI contracts are named Boost.UT tests;
the nine fatal contracts are named subject tests selected in fresh aborting
processes. No C++ test remains manual or registers a test/assertion in the
global suite.

Golden checks machine-check all 166 reviewed source-to-header-to-suite-to-selector
and rank/fatal mappings against generated CTest commands, labels, processor counts,
and timeouts. They reject unreviewed runner sources, missing or multiply
included registration headers, anonymous namespaces, non-inline header
registration functions, suite declarations in headers, captured/static suite lambdas,
duplicate names, unknown selectors, and registrations outside the reviewed
subject suite. Fresh reverse-order processes passed for all eight combined
subjects. The verbose audit passed all 23 normal aliases and required expected
active named-subject output per invocation plus exactly zero tests and zero
assertions in every process's global suite. MPI ranks write to separate
temporary audit logs, avoiding the launcher's character-level output
interleaving while preserving the exact generated command.
Fatal-wrapper unit regressions accept the
reviewed MPICH and OpenMPI native abort forms and reject an exact Rift marker
plus exit status one when native `MPI_Abort` evidence is absent.

The exact final Debug ASan/UBSan build used six jobs, completed in 66.32 s with
1,372,856 KiB per-process maximum RSS and zero swaps, and passed 52/52 CTests in
22.59 s. The exact final `-O3 -march=native -DNDEBUG` Release build used six
jobs, completed in 35.86 s with 1,015,836 KiB maximum RSS and zero swaps, and
passed 52/52 CTests in 19.04 s. Whole-tree Clang-Tidy 22.1.8 rebuilt the final
registration design with zero diagnostics in 90.41 s, 1,455,776 KiB maximum
RSS, and zero swaps. No
`NOLINT`, coverage exclusion, sanitizer suppression, or weakened oracle was
introduced. Doxygen 1.9.8 emitted no warning, and Sourcey 3.6.5 generated 136
documentation pages successfully.

The coverage-completeness contract preprocesses the guard source, discards
string/character literals, and requires all 18 manifest entries to occur in
active explicit class/function instantiations or
`static_assert(std::same_as<...>)` checks that the coverage target then compiles.
Comment-only, string-literal, inactive-`#if 0`, and bare-substring mutants fail.
The authoritative final coverage campaign required a new nonempty alias-indexed
profile after each of all 33 coverage aliases, including every fatal selector.
It retained all five production units and all 18 explicitly guarded
supported-template uses and reported:

| Metric | Covered / defined | Result |
|---|---:|---|
| Distinct physical lines | 2878 / 2878 | PASS |
| Canonical source definitions | 351 / 351 | PASS |
| Exact authored branch outcomes | 919 / 919 | PASS |
| Conservative LCOV branch projection | 918 / 918 | PASS |

The report is
`build/coverage-clang/coverage-profiles.nPzNx4/report/raw-summary.txt`; its
SHA-256 is
`c0c6470f801bdd1d6c7d62091efbeea195eae8a17d6c0111b0317f24f28f07db`.

The independent audit closure added one fail-closed focused-selection tooling
CTest and strengthened, without changing production code, the full 166-entry
mapping, eight-subject reverse-order inventory, rank-separated global-suite
oracle, and structural completeness-guard parser. The affected six-job builds
reported zero swaps: Debug 26.75 s with 1,150,340 KiB per-process maximum RSS,
Release 12.02 s with 930,504 KiB, coverage 9.92 s with 1,045,412 KiB, and
Clang-Tidy 40.11 s with 1,345,772 KiB and zero diagnostics. A committed exact
inventory requires each of the 202 pre-consolidation Boost.UT names once while
allowing the 32 reviewed additions; its rename mutant fails. Debug passed 54/54
CTest invocations in 23.68 s; Release passed 54/54 in 20.25 s. The exact
nonempty focused replay passed 15/15 in 5.56 s (Debug) and 4.38 s (Release).
The refreshed authoritative report at
`build/coverage-clang/coverage-profiles.14Jpkg/report/raw-summary.txt` retained
2878/2878 lines, 351/351 definitions, and 919/919 authored outcomes; its
SHA-256 remains
`c0c6470f801bdd1d6c7d62091efbeea195eae8a17d6c0111b0317f24f28f07db`.

The mandatory strict raw no-exclusion Clang-gcov/gcovr 7.0 diagnostic passed
52/52 CTests and reported 2364/2396 lines (98.7%), 635/642 functions (98.9%),
and 2200/3520 compiler-CFG branch outcomes (62.5%). Its JSON SHA-256 is
`1344f11e568791ef57f83b89c7db76084e222e02b65f12d0194763f5425bda11`.
The deterministic [branch inventory](subject-runner-raw-gcovr-branch-misses.tsv),
[line inventory](subject-runner-raw-gcovr-line-misses.tsv), and
[function inventory](subject-runner-raw-gcovr-function-misses.tsv) contain 620
branch locations totaling 1,320 uncovered compiler-CFG arcs, 32 line records,
and seven function records. Their SHA-256 values are respectively
`7b5c06cc0a83aecc5e8111abe5ce52f4a0a671d81685ab6f7c8ffe07a0458bf1`,
`b0cd5328aecc1f5047e27eb7475bd168c82b1033c3f09ffb32b358848cf686bd`,
and `4a615cef06f3544e99b1f7a2ac6278ad69694082c9348ee78699605370c58b55`.
This compiler-CFG diagnostic remains non-gating; the exact per-alias LLVM
campaign is authoritative.

The change is confined to tests, CMake/CTest registration, coverage tooling,
and contribution policy. It preserves the production source API, static-library
ABI, numerical representation, precision, tolerances, stopping criteria,
determinism, and compiler floating-point behavior. The repository's pre-release
rebuild-together compatibility mode therefore needs no migration path. Evidence
is limited to this Clang/MPICH workstation; OpenMPI/GCC CI, aggregate six-job
peak memory, and property/fuzz/mutation frameworks remain named gaps.

## Executable tutorial verification addendum

Three cumulative, standalone, two-dimensional MPI tutorials now exercise the
intended public workflow: run/phase-graph construction, distributed mesh and
space construction, and trial/seal/publish state updates. Each is a default
build target and a two-rank CTest with exact `tutorial;mpi` labels, processor
count, timeout, and a stable completion-line oracle. A golden matrix rejects
missing, additional, orphaned, multiply mapped, serial-only, test-helper, or
non-public tutorial programs. Configuring with `BUILD_TESTING=OFF` still built
all three targets and registered zero CTests.

The documentation preprocessor imports every displayed C++ region from the
compiled canonical source. Its unit checks reject missing, duplicate, nested,
unbalanced, empty, foreign, conditionally inactive, path-escaping, handwritten,
unused, multiply referenced, or fence-injecting snippets. Handwritten-fence
mutants cover backtick and tilde fences of length three or greater, zero through
three leading spaces, and case-insensitive `cpp`, `c++`, `cc`, and `cxx`
languages. Directives inside non-C++ fenced blocks also fail. Doxygen 1.9.8
emitted no diagnostic and Sourcey 3.6.5 generated 139 pages. The final six-job
Debug build passed 58/58 CTests in 23.64 s; Release passed 58/58 in 20.39 s.
Whole-tree Clang-Tidy 22.1.8 completed with zero
diagnostics, and format, shell syntax, Python syntax, `git diff --check`, and
strict snippet checks passed.

The layout verifier reads `MPIEXEC_EXECUTABLE` and `MPIEXEC_NUMPROC_FLAG` from
the configured CMake cache and requires every command to begin with that exact
launcher, rank flag, and the value two. Mutants for a direct executable command,
wrong launcher, wrong flag, and one rank all fail before the real CTest matrix
is checked. The semantic smoke oracles require Tutorial 1's exact one-call/rank
oriented compatibility trace; Tutorial 2's exact owner-cell partition, two
globally nonempty requests, requested-to-final support containment, and empty
regional schema; and Tutorial 3's exact binary field/regional values, epoch
transition, and level-set revisions. Intentional live mutants changing the
callback count to zero, requiring zero global gas cells, swapping the liquid/gas
center-classifier assignments, and expecting accepted gas values of 1.5 each
failed the corresponding two-rank CTest and named the violated invariant.
Restoring each source returned the tutorial set to 3/3. The classifier mutant is
rejected by an independent per-cell geometric oracle, rather than by comparing
two sets derived from the same assignment.

Tutorial smoke tests are intentionally outside the optimized unit-test coverage
manifest, and exact-label regressions protect that boundary. The authoritative
whole-source campaign remained 2878/2878 distinct lines, 351/351 canonical
definitions, and 919/919 authored branch outcomes. The conservative LCOV
projection remained 2878/2878 lines, 351/351 functions, and 918/918 branches.
No coverage exclusion or suppression was added.

The mandatory raw no-exclusion Clang-gcov/gcovr 7.0 diagnostic passed 58/58
CTest invocations and reported 2362/2396 lines (98.6%), 631/642 functions
(98.3%), and 2200/3520 compiler-CFG branches (62.5%). Its JSON SHA-256 is
`887c22c3aefd09fef1b583764585634af24b1fff355c329879d3a693e0410b38`.
The committed tutorial-campaign [branch
inventory](tutorial-raw-gcovr-branch-misses.tsv), [line
inventory](tutorial-raw-gcovr-line-misses.tsv), and [function
inventory](tutorial-raw-gcovr-function-misses.tsv) contain 620 branch locations
totaling all 1,320 uncovered arcs, 34 line records, and 11 function records.
Their SHA-256 values are respectively
`ebf7b3a48db0f423035fe3110d20ea0e172307d1e4dd9aa8e9ec1f275f63ab1e`,
`a4b94bb8ad6f1e431500056acb914c7fa53a116d5bdda7752dcb091137136724`,
and `53880cd343ea4d9046225f14e5299c36ace77ecedcda6fbd1f80183b8f75b86a`.
Regenerating them from the hashed JSON with `gcovr_miss_inventory.py` produced
byte-identical files. As documented for the shared static-library `.gcda`
files, this last-writer-unstable diagnostic is non-gating; the exact per-alias
LLVM campaign is authoritative.
