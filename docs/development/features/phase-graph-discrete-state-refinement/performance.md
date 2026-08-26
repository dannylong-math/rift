# Test-build performance record

## Decision and scope

The representative workload is a clean Debug sanitizer build of the complete
Rift library and test fleet. The optimization target is build wall time on the
project workstation at the user-approved limit of six concurrent build jobs.
The acceptance criteria were a deterministic reduction in compiled test
translation units and linked executables, no loss of test bodies or MPI rank
invocations, reduced measured CPU work, no observed per-process memory
regression, and a passing complete Debug test suite. The one observed wall-time
result is reported without claiming that a single sample characterizes timing
noise.

No production source, public API, ABI, numerical method, precision, tolerance,
iteration count, compiler floating-point behavior, or data format changed.

## Bottleneck evidence

The baseline built 141 automatically discovered serial test translation units
and 45 MPI test executables from 38 MPI sources. Fifteen compatible
`phase_graph_reject_*.cpp` files separately parsed the same Rift, deal.II, and
Boost.UT headers. Seven ordinary MPI sources were each compiled and linked
twice solely to register two- and three-rank CTest invocations. The baseline
log consequently contained 191 C++ compilation steps and 186 executable link
steps. During the final baseline phase, the duplicated MPI sources appeared as
separate object builds under the two rank-specific executable targets.

This evidence identifies repeated translation-unit parsing and test-target
link fan-out as the measured build-system bottleneck. It does not attribute the
cost to a production computation.

## Change

- Fifteen process-compatible phase-graph validation sources became one
  `phase_graph_validation_suite_00.cpp` translation unit. All 16 original
  Boost.UT case names and test bodies are retained under three documented
  groups:
  - `suite<"phase graph phase specification validation">`;
  - `suite<"phase graph interface specification validation">`;
  - `suite<"phase graph compatibility validation">`.
- `phase_graph_reject_id_exhaustion_00.cpp` remains an isolated executable
  because it manipulates finite allocator state and process-level identity.
- Seven pairs of two- and three-rank MPI CTests now reuse one compiled binary.
  Fatal MPI tests and other process-isolation contracts remain separate.
- The coverage manifest now requires each labeled CTest to invoke exactly one
  discovered instrumented executable and every discovered executable to have
  at least one labeled CTest. Coverage executes every CTest alias before one
  profile merge/export for that binary. Alias-indexed profile filenames avoid
  sequential overwrite while retaining the executable prefix used for profile
  collection.

## Environment

- Date: 2026-08-25
- Host: `dannylong-Latitude-7440`
- OS: Linux `7.0.0-28-generic`, x86-64
- CPU: 13th Gen Intel Core i7-1370P, 14 cores / 20 hardware threads
- Memory: 30 GiB RAM, 8 GiB configured swap
- Compiler: Clang 22.1.8
- CMake: 3.28.3, Unix Makefiles
- Configuration: Debug, `ENABLE_SANITIZERS=ON`
- Dependencies: deal.II 9.8.0; Boost.UT v2.3.1
- Build jobs: exactly 6
- Affinity: operating-system default; no explicit pinning
- Warm-up: none; each measured build directory started without compiled output
- Repetitions: one baseline and one candidate clean build
- Baseline commit: `253c25204068a5794a36916d533331a654e7cb3f`

The dependency source and installed deal.II tree were held constant. Baseline
and candidate were built in separate directories on the same local filesystem
and during the same work session.

## Reproduction commands

Baseline configuration and build:

```console
/usr/bin/time -v -o performance-logs/baseline-configure.time \
  cmake --preset debug \
  -Ddeal.II_DIR=/home/dannylong/research/sharp_interface/rift/.dependencies/install/dealii/debug/lib/cmake/deal.II \
  -DFETCHCONTENT_SOURCE_DIR_UT=/home/dannylong/research/sharp_interface/rift/build/debug/_deps/ut-src
/usr/bin/time -v -o performance-logs/baseline-build.time \
  cmake --build --preset debug --parallel 6
```

Candidate configuration, build, and test:

```console
/usr/bin/time -v -o performance-logs/candidate-configure.time \
  cmake --preset debug -B build/candidate-debug \
  -Ddeal.II_DIR=/home/dannylong/research/sharp_interface/rift/.dependencies/install/dealii/debug/lib/cmake/deal.II \
  -DFETCHCONTENT_SOURCE_DIR_UT=/home/dannylong/research/sharp_interface/rift/build/debug/_deps/ut-src
/usr/bin/time -v -o performance-logs/candidate-build.time \
  cmake --build build/candidate-debug --parallel 6
/usr/bin/time -v -o performance-logs/candidate-ctest.time \
  ctest --test-dir build/candidate-debug --output-on-failure
```

MPI tests require permission to create localhost sockets in a restricted
sandbox.

## Raw summary

| Metric | Baseline | Candidate | Absolute delta | Relative delta |
|---|---:|---:|---:|---:|
| Build wall time | 651.67 s | 584.63 s | -67.04 s | -10.29% |
| User CPU time | 3733.71 s | 3336.98 s | -396.73 s | -10.63% |
| System CPU time | 101.37 s | 90.17 s | -11.20 s | -11.05% |
| CPU utilization | 588% | 586% | -2 percentage points | -0.34% |
| GNU-time maximum RSS, per process | 1,229,124 KiB | 1,228,780 KiB | -344 KiB | -0.03% |
| GNU-time reported swaps | 0 | 0 | 0 | 0% |
| C++ compilation steps | 191 | 170 | -21 | -10.99% |
| Executable link steps | 186 | 165 | -21 | -11.29% |
| Test executables | 186 | 165 | -21 | -11.29% |

The observed candidate wall-time ratio is 1.115x. GNU `time` reports the
maximum resident set of an individual process in this multi-process build, not
the aggregate memory occupied by all six concurrent jobs. It was effectively
unchanged. Aggregate peak memory and OOM headroom were not measured. The lower
work count does reduce the duration for which compiler/linker jobs are active,
but this comparison does not quantify aggregate memory-pressure reduction.

The test registration count changed from 199 to 187: consolidating 15 serial
CTest executables into one removed 14 registrations, and two coverage-tooling
tests were added. All seven two-/three-rank MPI aliases remain registered.

## Correctness and numerical equivalence

The consolidated source contains the same 16 unique Boost.UT `_test` names as
the 15 replaced files, with no missing or added test case. The candidate Debug
suite passed 187/187 CTests in 57.27 seconds:

- serial: 127 tests;
- MPI: 45 tests;
- tooling: 15 tests.

The new tooling oracles cover a valid two-CTest/one-executable mapping, a
labeled CTest that invokes no discovered executable, an unregistered
instrumented executable, execution of both aliases, and distinct deterministic
profile paths for the aliases. Numerical production code is unchanged, so the
applicable equivalence evidence is the unchanged test bodies/oracles and full
sanitized Debug pass.

An independent authoritative candidate coverage run passed after this
comparison. The completeness guard found all 5 production units and 18
supported template entries. Exact guarded LLVM coverage was 2878/2878 physical
lines, 347/347 canonical source definitions, and 919/919 canonical authored
branch outcomes; the conservative LCOV projection was 918/918. Every one of
the seven shared MPI binaries generated profiles from both its two-rank and
three-rank CTest invocations, followed by exactly one export for that binary.
Release remains a final feature quality gate rather than evidence from this
focused Debug performance comparison.

## API, ABI, and compatibility

Source API and ABI are unaffected. The change is confined to test sources,
CTest/CMake registration, coverage tooling, and contributor policy. The
existing coverage COMDAT rule remains protected: profiles are merged and
exported once per exact independently linked binary, never across binaries.

## Uncertainty, target coverage, and limitations

Only this workstation and Debug sanitizer configuration were measured. The
single before/after samples do not provide a variance estimate, confidence
interval, cold-cache control, or scaling curve, so they cannot establish how
much of the observed 10.29% wall reduction exceeds incidental variation. The
deterministic 21-step compile/link reduction and 10.63% lower measured user CPU
time are the stronger evidence of reduced build work. GNU-time maximum RSS is
per process; aggregate peak memory, system-wide swap activity, and OOM headroom
were not measured. No claim is made for Release, coverage,
clang-tidy, another compiler, a cluster, or a GPU environment.

The remaining dominant cost is parsing and instantiating template-heavy
headers in the other 126 serial test translation units and 31 unique MPI
translation units. Further consolidation should proceed only by coherent
subject/process contract and should be benchmarked in another clean comparison;
unrelated tests and fatal/process-isolated oracles must not be combined merely
to reduce target count.

## Full subject-runner promotion

The user accepted the subject-runner technique after the focused smoke and
authorized its extension to every C++ test on 2026-08-26. The representative
workload remained a fresh complete Debug ASan/UBSan build, with a second fresh
Clang source-coverage build to measure the coverage target graph. Both used
Clang 22.1.8, the same deal.II 9.8.0 and Boost.UT 2.3.1 trees, the workstation
and runtime conditions above, no affinity or warm-up, and exactly six parallel
jobs. Each value is one clean observation, so deterministic work-count changes
are stronger evidence than the precise wall-time ratio.

The final layout has ten ordinary subject runners, one MPI-infrastructure
runner, and one separately whole-archive-linked coverage-completeness runner.
It physically includes 166 registration headers instead of compiling each
behavior file independently. The 12 serial, 11 ordinary subject/rank, and nine
fatal process aliases preserve fresh-process lifecycle and abort contracts.
Fatal operations remain separate aliases, but reuse their subject binary and
select exactly one named aborting Boost.UT test. No production source or
runtime computation changed.

The exact final clean commands were:

```console
cmake --build --preset debug --target clean
/usr/bin/time -v -o /tmp/rift-subject-runner-exact-final-debug.time \
  cmake --build --preset debug --parallel 6
ctest --preset debug --output-on-failure

cmake --build --preset coverage-clang --target clean
/usr/bin/time -v -o /tmp/rift-subject-runner-exact-final-coverage.time \
  cmake --build --preset coverage-clang --parallel 6
./scripts/clang_source_coverage.sh
```

| Configuration and metric | Audited pre-runner baseline | Focused smoke | Final all-C++ layout |
|---|---:|---:|---:|
| Debug wall time | 607.77 s | 199.95 s | 66.32 s |
| Debug user CPU | 3463.30 s | 1085.10 s | 285.82 s |
| Debug system CPU | 95.67 s | 31.31 s | 7.00 s |
| Debug per-process maximum RSS | 1,229,900 KiB | 1,229,236 KiB | 1,372,856 KiB |
| Debug compile actions | 170 | 56 | 16 |
| Debug test executable links | 165 | 51 | 11 |
| Coverage wall time | 311.55 s | 137.86 s | 31.82 s |
| Coverage user CPU | 1733.83 s | 736.62 s | 126.08 s |
| Coverage system CPU | 90.09 s | 37.94 s | 6.73 s |
| Coverage per-process maximum RSS | 1,073,436 KiB | 1,073,132 KiB | 1,162,980 KiB |
| Coverage compile actions | 171 | 57 | 17 |
| Coverage test executable links | 166 | 52 | 12 |
| GNU-time reported swaps | 0 | 0 | 0 |

The final Debug build is 9.164x faster than the audited baseline (541.45 s,
89.09% less wall time) and 3.015x faster than the focused smoke. The final
coverage build is 9.791x faster than baseline (89.79% less wall time) and
4.332x faster than the smoke. Per-process compiler RSS increased by 142,956
KiB (11.62%) for Debug and 89,544 KiB (8.34%) for coverage because each subject
translation unit is larger. GNU time does not measure aggregate memory across
the six concurrent jobs; no build swapped or encountered OOM, but aggregate
peak memory remains unmeasured.

The full final Debug suite passed 52/52 CTests in 22.59 s. Structural evidence
records 234 unique named Boost.UT tests and 1,148 `expect()` sites, preserving
all 202 prior names and 1,113 prior expectation sites while additively naming
the former manual contracts. The exact LLVM campaign retained all five
production units and 18 template uses and passed 2878/2878 lines, 351/351
definitions, and 919/919 authored outcomes. These results establish unchanged
test and numerical behavior for the exercised environment; they are not a
runtime-kernel benchmark or a claim about another compiler, MPI implementation,
cluster, or GPU.

The remaining build bottleneck is the largest combined subject translation
units and the five production units. Further consolidation would cross public
object or process-contract boundaries and is not supported by this benchmark.

## Executable tutorial addendum

The reviewed learning path adds three deliberately separate translation units:
`tutorial-001.cpp`, `tutorial-002.cpp`, and `tutorial-003.cpp`. They are user
programs, not another fragmentation of the unit-test fleet, so keeping each
tutorial independently compilable is part of its teaching contract. The default
Debug graph consequently grows from 16 to 19 compile actions and from 11 to 14
executable links.

One clean six-job Debug observation on the final skeptic-reviewed candidate and
the same Clang 22.1.8 workstation took 85.26 s wall time, 420.02 s user CPU, and
10.59 s system CPU. GNU time reported 1,371,196 KiB maximum RSS for one process
and zero swaps. Compared with the 66.32 s final subject-runner observation, this
is 18.94 s (28.56%) more wall time.
The measurement is one sample with no cache control or variance estimate; it
shows the cost is still close to the accepted one-minute build scale, not a
general performance distribution. All builds used exactly six parallel jobs.
