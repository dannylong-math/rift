# Verification and Validation Report: Phase-graph and discrete-state foundations

## 1. Context of use

- **Decision supported:** whether the integrated pre-release Rift foundations are
  credible enough to serve as the deterministic topology, immutable mesh/space,
  and versioned state substrate for later sharp-interface solver development.
- **Consequence of error:** a wrong graph identity, support closure, DoF layout,
  or state transition could silently associate physics with the wrong phase,
  violate conformity, or corrupt later time-history state. The present decision
  is development-facing; these foundations are not approved for physical
  prediction or decision-critical engineering use.
- **Quantities of interest:** canonical graph representation and identifiers;
  graph/run/mesh/space/state provenance; closed phase-support cell sets; scalar
  Q1 DoF totals; hanging-constraint residuals for the constant and coordinate
  basis; immutable snapshot identity and lifetime; state identities and retained
  snapshot availability; exact IEEE-754 binary64 regional values and level-set
  revision transitions on one, two, and three ranks.
- **Domain/regime:** the implemented C++23 API, deal.II 9.8 distributed 2D/3D
  continuous-Q1 spaces, MPICH/Hydra execution on one to three local ranks, and
  the canonical adaptive meshes encoded by the repository tests. Full `hp`, DG,
  GPU, other MPI implementations, and complete flow solves are outside scope.
- **Required accuracy/uncertainty:** graph, provenance, identity, set, count, and
  representation comparisons are exact. Hanging constraints must reproduce
  constants and each coordinate-linear polynomial with absolute residual below
  `1e-13`. There is no statistical sampling uncertainty; only finite
  configuration coverage and floating-point roundoff enter this campaign.
- **Supported claims:** only the contract-level software and code-verification
  claims C1--C7 below, within the tested dimensions, element degree, MPI ranks,
  compiler/build configurations, and canonical fixtures.
- **Excluded claims:** accuracy or convergence of a PDE solution, adequacy of a
  physical multiphase model, production scalability, performance, reliability
  on untested backends, and fitness for engineering decisions.

No external VVUQ standard is adopted or claimed.

## 2. Implemented model and numerical foundations

There is no complete governing PDE, initial/boundary-value problem, interface
reconstruction, time integrator, or physical closure in this feature. The
implemented numerical foundation is a continuous-Galerkin mesh/space substrate:
owner-local phase support is closed across adaptive hanging interfaces to a
least fixed point, inactive components use `FE_Nothing`, and full-background
level-set spaces remain active. Published state snapshots are immutable and
carry run/store/mesh/space and revision identities. Regional and level-set
representation semantics use exact binary64 bit patterns, intentionally
distinguishing signed zeros and distinct NaN encodings.

After the first integrated V&V run at `d0d6287`, the final quality cleanup
replaced incomplete aggregate initialization with explicit constructors and made
`StateSnapshotStamp` non-default-constructible. Every stamp must now receive its
space, store, snapshot, and optional publication identities; index zero remains a
valid identity rather than an invalid sentinel. The cleanup also moved named
Boost.UT suite registration after MPI initialization. These are deliberate
pre-release source/API and test-harness invariant changes, not changes to the
closure algorithm, state transition semantics, precision, tolerance, collective
value algorithm, or floating-point mode. Because production/API code changed,
the full integration evidence and focused C0--C7 campaign were re-executed at
`afe95dd` rather than inheriting the earlier result.

The decisive canonical adaptive oracle requests one fine child adjacent to a
coarse face. Conformity requires 3 supported cells with 1 closure addition in
2D and 5 supported cells with 3 additions in 3D. Independent mesh topology and
Q1 vertex counting gives field-space totals of 8 and 22 DoFs, respectively;
the full-background level-set totals are 11 and 31.

## 3. Applicability matrix

| Category | Status | Justification | Evidence |
|---|---|---|---|
| System integration | REQUIRED | Graph, mesh, space, MPI collectives, and state ownership interact across component boundaries. | Full Debug/Release suites plus focused serial, two-rank, and three-rank contract tests. |
| Code verification | REQUIRED | Adaptive closure and constrained Q1 construction implement mathematical set/interpolation properties; retention and representation logic implements exact state-machine/bit contracts. | Independent exact support/count oracles, constant/coordinate polynomial reproduction, a symbolic retention model, and hexadecimal bit-pattern corpora. |
| Calculation/solution verification | NOT APPLICABLE | No PDE/ODE calculation, timestep, iterative solve, or computed solution is produced. | Revisit when a complete calculation exists. |
| Model validation | NOT APPLICABLE | No physical-model prediction is made and no experimental comparison is meaningful. | Revisit when governing physics and validation quantities exist. |
| Uncertainty quantification | NOT APPLICABLE | The tested contracts are discrete/exact and no uncertain physical inputs or prediction intervals exist. Floating-point residual is bounded directly. | Revisit for numerical/parameter/model uncertainty in a solver. |
| Data-pipeline validation | NOT APPLICABLE | No inference or scientific-data transformation pipeline is implemented. | None. |
| Runtime performance | NOT APPLICABLE | This feature establishes semantics, not solver throughput/latency. The separately requested developer test-build optimization is build-system evidence rather than scientific V&V. | Record build-time evidence separately in `performance.md`; benchmark solver runtime when a complete calculation exists. |

## 4. Predeclared claims and acceptance criteria

These criteria were frozen in the working report after the T7 evidence checkpoint
`fc7e413` and before the final integrated commands in section 10. The report is
committed after execution so it can contain the exact tested revision and results;
the earlier task-level results in `verification.md` are supporting evidence, not a
substitute for this final campaign. A test-process crash, sanitizer finding,
timeout, MPI-launcher failure, or failed assertion is a failure, not an outlier.
Commands are run once because all fixtures are deterministic and seedless.
Execution stops for investigation on the first contract failure; criteria will not
be relaxed post hoc.

| Claim | Metric and independent oracle | Acceptance criterion | Stop rule | Final result |
|---|---|---|---|---|
| C1. Phase graph and collectives are deterministic and provenance-safe. | Canonical JSON, compatibility trace/order, IDs/orientation under input permutations; cross-run/graph reference rejection; rank agreement and replicated mismatch handling. | Exact equality for valid permutations on 1/2/3 ranks; foreign references and divergent replicated inputs/callback results are rejected with the declared deterministic code; all focused tests exit 0. | Stop at first disagreement, timeout, or unexpected acceptance. | **PASS** |
| C2. Mesh snapshots are immutable and provenance/lifetime safe. | Identity/provenance, global cell IDs, communicator ownership, caller-input and caller-communicator lifetime on serial and distributed fixtures. | Snapshot observations remain unchanged and usable after caller objects/communicators end; exact rank agreement and correct rejection of null/similar/unrelated contexts; all focused tests exit 0. | Stop at first mutation, dangling-lifetime symptom, sanitizer finding, or rank disagreement. | **PASS** |
| C3. Adaptive phase support reaches the serial least fixed point and is partition invariant. | Exact CellId sets from an independent topology oracle, serial versus 2/3-rank union, and a multilevel cascading oracle. | With the serial two-cell request, 2D has 1 closure addition/3 final cells and 3D has 3 additions/5 final cells. With the MPI one-cell seed, the same final sets require 2 and 4 additions. MPI global unions equal the serial topology oracle, including cross-owner and cascading closure; all focused tests exit 0. | Stop on set/count mismatch, nonconvergence at the approved bound, or rank disagreement. | **PASS** |
| C4. Discrete spaces have analytic cardinality and reproduce the Q1 invariant space. | Analytic vertex-count DoF totals; constraint residual after distributing independently assigned `1, x_0, ..., x_(dim-1)`. | Phase field totals are exactly 8 (2D) and 22 (3D); level-set totals exactly 11 and 31; hanging constraints are nonempty and every residual is `<1e-13` in serial and MPI fixtures. | Stop on count mismatch, vacuous constraints, non-finite residual, or residual `>=1e-13`. | **PASS** |
| C5. State identity, transition, retention, and lifetime semantics are deterministic. | Independent finite-state retention model, exact five-component identity sequences, serial/2/3-rank agreement, eviction/pin/discard behavior, and external immutable-handle lifetime. | Model and implementation agree for capacities 0/1/2/`SIZE_MAX` and all 36 two-action suffixes after an initial seal in 2D and 3D (288 deterministic sequences); rejected operations consume no identity; accepted identities agree by rank; retained/evicted availability and external-handle lifetime match the contract; all focused tests exit 0. | Stop on model divergence, identity drift/reuse, invalidation of an external immutable handle, or rank disagreement. | **PASS** |
| C6. Regional state has exact representation semantics and partition-invariant reads. | Independent hexadecimal `uint64_t` corpus bit-cast to binary64, backend/cache inspection, rank gathers, reversed communicator, rejection/retry atomicity. | Every accepted getter reproduces the exact 64 bits on every rank (including signed zero, subnormals, infinities, and NaN payload/sign); rank-zero ownership follows the layout communicator; divergent writes reject without cache/backend/identity drift; all focused tests exit 0. | Stop on any bit mismatch, partial commit, wrong owner, timeout, or identity drift. | **PASS** |
| C7. Level-set revision semantics are exact and partition invariant. | Exact bit-pattern changes on owner-local entries, including nonroot/middle-rank edits, signed zero and NaN variants; regional-only comparison as a negative control. | Identical bit patterns leave the revision unchanged; any changed bit pattern advances exactly one agreed revision; simultaneous multi-rank changes do not cancel; regional-only updates do not advance it; exhaustion/retry remains atomic; all focused tests exit 0. | Stop on missed/spurious/multiple revision, rank disagreement, parity cancellation, or atomicity failure. | **PASS** |
| C0. Unit/integration entry condition. | Registered CTest results under Debug ASan/UBSan and optimized Release. | 100% of registered tests pass in both presets, with zero sanitizer findings. | A failure blocks contract-level PASS even if focused evidence passes. | **PASS** |

Coverage, static analysis, and unit-test adequacy are recorded in
`verification.md`; they are entry/gate evidence and do not replace C3--C7's
mathematical and representation-level oracles.

## 5. Reproducibility manifest

- **Implementation evidence checkpoint:**
  `fc7e413b1c063b69782b77f8c62d8125849cc05e` on branch
  `feature/phase-graph-discrete-state-refinement`.
- **Final tested revision/branch/dirty state:**
  `afe95dd49a898d8ac402146d4986c8e29dac6714` on the same branch. The independent
  audit began from a clean tracked tree, and the focused V&V refresh also began
  with no working-tree changes.
- **Host:** Ubuntu kernel `7.0.0-28-generic`, x86-64, Intel Core i7-1370P,
  20 logical CPUs, one NUMA node.
- **Toolchain:** CMake 3.28.3; GCC/G++ 13.3.0; Clang 22.1.8; Ninja 1.11.1;
  MPICH/Hydra 5.0.0; C++23; repository-pinned deal.II 9.8.0 and dependencies.
- **Configurations:** CMake presets `debug` (Debug, ASan/UBSan enabled) and
  `release` (`-O3 -march=native -DNDEBUG`). `release-max` is deliberately
  excluded because it changes floating-point semantics.
- **Runtime:** local process execution with the required localhost MPI socket
  allowance; MPI rank counts 2 and 3 as registered; default thread settings; no
  accelerator. Builds were sequential and used exactly six concurrent jobs.
- **Inputs/data/provenance:** deterministic graph permutations, adaptive meshes,
  transition sequences, and hexadecimal binary64 corpora are source-controlled
  in `tests/` and therefore identified by the tested Git revision. No external
  dataset, generated reference file, preprocessing, data license, or separate
  data checksum applies.
- **Seeds/repetitions:** no random seeds; one decisive execution per command.
- **Raw artifacts:** the independent final-audit build and full-suite logs are
  under `build/audit-afe95dd/`: `debug-build.log` (SHA-256
  `6a2532ba011008035474f628bc5131b4540c731870284ea6fe4e64f15c15f10d`),
  `debug-ctest.log` (`a4656ca8145440582431e6d50cbc8c180d41810846df8d196dfbe737f18743b1`),
  `release-build.log` (`06c5b405a908b1b2e581f418bdc377bbcd6a98b88b1df09ad4773518eb691880`),
  and `release-ctest.log`
  (`a18e856837473c765ce0d22b1304cd781e643072110273695e11f6dd4626db18`).
  The refreshed focused logs are
  `build/debug/Testing/Temporary/LastTest.log` (SHA-256
  `1a8f0732336730ceeaca47e0207de85bc38511d0b98269da802deaeff4cf5f48`)
  and `build/release/Testing/Temporary/LastTest.log` (SHA-256
  `4fc58a3c9c1c41e94a2113860248966f94afb05a830daf61d8f810e60769d1e5`).
  Command output was also retained in the audit and V&V execution transcripts.

## 6. Experiments and results

### 6.1 Debug and optimized integration entry condition

- **Purpose and oracle:** establish that every registered serial, MPI, fatal-path,
  tooling, and coverage-completeness test passes on one integrated tree. Debug
  uses the repository ASan/UBSan configuration; Release checks the same contracts
  after optimization. CTest's registered expectations and the test assertions are
  the oracle.
- **Procedure:** build each preset with at most six concurrent jobs, run its full
  CTest inventory, and retain the CTest log. No failed run is discarded.
- **Raw result:** the independent final audit built the exact candidate with six
  jobs: Debug PASS in 8:19.79 wall time and Release PASS in 4:27.19. Debug then
  passed 187/187 registered tests in 56.78 s (127 serial, 45 MPI, 15 tooling),
  while Release passed 187/187 in 50.33 s with the same label counts. No
  sanitizer finding, test failure, timeout, or unexpected child exit was
  reported. After the C++ subject-runner promotion and its audit closure, the
  consolidated fleet passed 54/54 CTests in Debug (23.68 s) and Release
  (20.25 s), again with no sanitizer finding, timeout, or unexpected exit.
- **Uncertainty/error analysis:** deterministic and seedless; this is regression
  and system-integration evidence, not an independent numerical oracle.
- **Acceptance result:** C0 **PASS**.

### 6.2 Deterministic graph, provenance, and ownership contracts

- **Purpose and independent oracle:** compare canonical graph representations,
  callback traces, interface orientation, and identity/provenance outcomes under
  independently generated declaration permutations and cross-context misuse.
  Multi-rank cases place an outlier on an intermediate rank so endpoint-only
  agreement cannot pass. Mesh lifetime cases destroy or release caller-owned
  inputs before observing the retained snapshot.
- **Procedure:** execute the focused C1/C2 tests in Debug and Release after the
  corresponding full suite. Require exact serialized values, identities, error
  codes, and rank agreement.
- **Raw result:** after the final cleanup/API invariant change, every selected
  graph and mesh test passed again. After consolidation, the fail-closed selector
  first proved an exact nonempty set and then passed 15/15 subject/rank aliases
  in Debug (5.56 s) and Release (4.38 s). Those aliases include all 28 former
  source-level focused cases, including two- and three-rank graph agreement and
  distributed mesh identity/ownership cases.
- **Uncertainty/error analysis:** all comparisons are discrete and exact. The
  finite fixture set does not prove every possible graph or communicator lineage.
- **Acceptance result:** C1 and C2 **PASS**.

### 6.3 Adaptive support closure and constrained Q1 space

- **Purpose and independent oracle:** verify the implementation against mesh
  topology and polynomial properties rather than against another call to the
  production closure routine. Exact `CellId` sets and analytic Q1 vertex counts
  are specified in the tests. Independently assigned constant and coordinate
  fields must be invariant after applying hanging-node constraints.
- **Procedure:** execute serial, two-rank, and three-rank adaptive-closure cases,
  including a cross-owner request and multilevel cascade. Compare MPI global
  unions with the serial topology oracle; then inspect nonempty constraints and
  the residual for `1, x_0, ..., x_(dim-1)`.
- **Raw result:** serial 2D/3D closure, analytic DoF, nonvacuous constraint, and
  polynomial-reproduction assertions passed in both presets. Two- and
  three-rank global-union cases and the cascading-closure case also passed in
  both focused replays.
- **Uncertainty/error analysis:** set and cardinality results are exact. The
  `1e-13` residual bound is fixed before execution and is intended only to absorb
  binary64 roundoff in an otherwise exactly reproducible Q1 polynomial space; no
  mesh-refinement order is inferred because no PDE solution is computed.
- **Acceptance result:** C3 and C4 **PASS**.

### 6.4 Versioned state, retention, and exact representations

- **Purpose and independent oracle:** compare bounded snapshot retention with an
  independent symbolic finite-state model and compare regional/level-set values
  with hexadecimal `uint64_t` patterns, not production floating-point equality.
  Gathered values expose every rank's representation. Reversed-communicator,
  intermediate-rank, rejection/retry, exhaustion, and external-handle cases test
  ownership, agreement, atomicity, and lifetime.
- **Procedure:** execute the focused serial, two-rank, and three-rank state cases
  in Debug and Release. The retention model covers capacities 0, 1, 2, and
  `SIZE_MAX` with all 36 two-action suffixes after an initial seal in each of 2D
  and 3D (288 deterministic sequences). Representation cases
  include signed zeros, subnormals, finite values, infinities, quiet/signaling
  NaNs, payload changes, and sign changes.
- **Raw result:** the symbolic retention model, exact identity allocator,
  external-lifetime, serial representation, two-/three-rank agreement, revision,
  and global-ID cases all passed in both focused replays. Their assertions are
  included in the common 15/15 Debug and 15/15 Release subject-alias results
  above.
- **Uncertainty/error analysis:** identities, model states, and representations
  are exact. The finite sequence length and rank counts bound the conclusion;
  execution once is sufficient for the deterministic oracle but does not measure
  intermittent hardware or MPI reliability.
- **Acceptance result:** C5, C6, and C7 **PASS**.

## 7. Anomalies and investigations

The original `d0d6287` campaign observed a benign `GLOB mismatch!` regeneration
after intentional test-file consolidation. More importantly, the later cleanup
changed public construction and completeness invariants, so the original tested
revision could not remain the final provenance claim. This was classified as an
API/invariant evidence change, not a mathematical-model or numerical-method
change. The independent full Debug/Release audit and the exact focused replays
were therefore rerun at `afe95dd`. No scientific, numerical, sanitizer,
assertion, MPI, timeout, or process-exit anomaly occurred in the refreshed
evidence.

## 8. Validation domain and extrapolation limits

Any PASS applies only to the tested discrete foundations: 2D/3D continuous Q1,
canonical adaptive fixtures, binary64 state values, MPICH on up to three local
ranks, and the named compiler configurations. It does not establish a general
proof for arbitrary partitions, mesh topologies, polynomial degrees, MPI
implementations, compiler versions, or hardware. Deterministic examples reduce
risk for the specified contracts but do not exhaust every possible graph or
state-history sequence.

## 9. Conclusions

- **Strongest defensible conclusion:** **PASS.** At revision `afe95dd`, the
  phase-graph, immutable mesh/space, adaptive Q1 support, and versioned-state
  foundations satisfy C0--C7 for the tested deterministic 2D/3D fixtures,
  binary64 representations, MPICH ranks, and Debug/Release configurations. This
  supports their use as a development substrate for later solver work; it does
  not support a physical-prediction or decision-critical engineering claim.
- **Residual risk:** finite mesh, partition, sequence, compiler, and MPI coverage
  leaves configuration risk. Exact agreement on these fixtures is not a proof for
  arbitrary topology, partition count, element degree, compiler, MPI
  implementation, or hardware. The campaign provides no PDE error estimate,
  physical-model discrepancy, or uncertainty interval because no such
  calculation or model exists yet.
- **Follow-up evidence:** a complete solver will require solution verification;
  a physical model will require uncertainty-aware validation against independent
  experimental evidence. A second MPI implementation and broader partition/topology
  study would strengthen the present contract-level inference.

## 10. Reproduce

The independent auditor first rebuilt and ran the full suites on the exact final
candidate using the first four commands below. The V&V scientist then reused
those unchanged candidate builds and ran the two exact focused commands. The
focused selector maps the former source-level cases to 15 consolidated
serial/two-rank/three-rank subject aliases. Before running them it queries
CTest's JSON model and fails unless that exact, nonempty set is selected; this
prevents a stale regular expression from succeeding after selecting zero tests.

```sh
cmake --build --preset debug --parallel 6
ctest --preset debug --output-on-failure
cmake --build --preset release --parallel 6
ctest --preset release --output-on-failure
/usr/bin/time -p python3 tests/tooling/subject_runner_focused_vv_00.py --build-dir build/debug --run
/usr/bin/time -p python3 tests/tooling/subject_runner_focused_vv_00.py --build-dir build/release --run
```

## Tutorial addendum

The three executable tutorials add no equation, constitutive model, numerical
scheme, tolerance, or physical-prediction claim. Their collective smoke oracles
verify that the documented public setup and state-lifecycle workflow executes
on two MPI ranks; they are software verification and documentation evidence,
not calculation verification or model validation. The C0--C7 evidence and its
domain therefore remain unchanged. Future solver tutorials will require new
solution-verification and, where physical claims are made, validation evidence.
