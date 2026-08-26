---
title: Contributing
description: Focused test layout, verification commands, and documentation expectations for Rift changes.
---

# Contributing

Rift favors changes whose contracts can be understood from a small number of
nearby files. An implementation change should normally include focused tests,
public API comments, and an update to the relevant architecture page.

## Unit-test layout

Every C++ test uses Boost.UT's three-level hierarchy: `expect()` assertions
check individual results, named `_test` cases collect assertions for one
behavior, and one `suite<"Primary public object or cohesive topic">` groups the
tests for their subject. Tests may be nested when that makes a complex
contract clearer. No actual test or assertion belongs to Boost.UT's global
suite; verbose output may show only its empty `0 asserts in 0 tests` summary.

Test bodies are physical registration headers named

```text
<subject>_<behavior>_<two-digit-sequence>.hpp
```

Each registration header starts with `#pragma once`, uses a unique
`rift_test::<stem>` namespace (`rift_test::mpi::<stem>` under `tests/mpi/`),
defines inline helpers, and exposes `inline void register_tests()`. It does not
use an anonymous namespace or declare a suite. Keep support/detail objects in
the primary public object's suite; for example, `SpaceDraft` is tested in
`suite<"Space registry">`.

Only reviewed `*_run_tests.cpp` files are C++ test executable sources. Each
runner physically includes compatible registration headers for one subject,
creates exactly one automatic captureless suite after the required MPI
lifecycle setup, registers the selected tests exactly once, and calls
`boost::ut::cfg<>.run()` once. Explicit fail-closed selectors let fresh serial,
MPI-rank, lifecycle, and fatal CTest processes reuse the same compiled subject
binary. This reduces repeated parsing of template-heavy Rift/deal.II headers
without hiding the named behavior or combining unrelated subjects.

Genuinely multi-rank registration headers belong under `tests/mpi/`. Add a
reviewed subject/rank alias with `rift_add_mpi_alias()` in
`tests/CMakeLists.txt`; keep the old source-level behavior name as the exact
Boost.UT `_test` name rather than another CTest executable. The helper launches
through CMake's discovered MPI launcher, declares the rank count and timeout,
and never hard-codes `mpirun` or `mpiexec`. Run the MPI subset with:

```console
ctest --preset debug -L mpi --output-on-failure
```

Fatal operations remain distinct process aliases because the external process
result is part of the oracle. The selected aborting case is still a named test
in its subject suite, and the wrapper requires the exact Rift marker, native
MPI-abort evidence, and expected status. The whole-archive coverage guard is a
separately linked runner for the same process-contract reason. Update the
reviewed source/registration/selector/rank golden matrix and structural layout
verifier whenever adding a registration header, runner, selector, or alias.

## Executable tutorial layout

The progressive user path lives in standalone sources named
`tutorials/tutorial-NNN.cpp`. Numbering uses three digits so source files and
documentation remain in teaching order. Tutorials build by default, use only
public Rift APIs, and do not include test helpers or another tutorial source.

Tutorials are MPI applications rather than Boost.UT unit tests. Each keeps an
MPI RAII object alive longer than every Rift object, uses `MPI_COMM_WORLD`, and
runs as a two-rank CTest labeled `tutorial;mpi` unless its lesson has a reviewed
reason for another rank count. Explicit non-`assert` checks and a stable
completion line make the executable itself a lightweight smoke test:

```console
ctest --preset debug -L tutorial --output-on-failure
```

The narrative page `docs/tutorials/tutorial-NNN.md` imports every displayed
C++ block from a named active region of its compiled source:

```text
<!-- rift:snippet tutorial-NNN.meaningful-name -->
```

The Sourcey preprocessor rejects handwritten C++ fences, missing or duplicate
directives, unused or conditional source regions, and malformed markers. Add
the source, page, target, and CTest contract to
`tutorials/tutorial_matrix.json`; the tutorial layout verifier keeps this
matrix separate from the optimized Boost.UT subject-runner matrix. Tutorial
executables are also deliberately outside the unit-test LLVM executable
manifest even though their CTests carry the honest `mpi` label.

After implementing a change, run the complete test suite and calculate line,
function, and branch coverage for first-party code in `include/rift/` and
`src/`. All three authoritative exact LLVM metrics must be 100% before the
implementation is complete. If that guarded report is below 100%, identify the
uncovered behavior, add focused tests with meaningful oracles, and repeat the
tests and coverage reports. A lower strict raw gcovr diagnostic must be
published and investigated, but it does not itself fail this gate.

The primary source-based report uses Clang's coverage mapping and gives each
process, including every MPI rank, a separate raw profile:

```console
cmake --preset coverage-clang
cmake --build --preset coverage-clang --parallel 6
./scripts/clang_source_coverage.sh
```

The script pairs each independently linked test executable with only the raw
profiles produced by that test, including a separate `%m`/`%p` profile for each
MPI rank. It fails on any `llvm-cov` diagnostic, validates LLVM's
source-definition and branch totals, and unions the paired first-party exports
into a normalized LCOV trace. The printed metrics count distinct physical
source lines and linked source definitions rather than template instantiations;
they report exact LLVM branch outcomes separately from the conservative
LCOV-representable projection. Before execution, the script checks that every
registered unit-test serial, MPI, and coverage-only CTest invokes exactly one
discovered instrumented executable and that every discovered unit-test
executable has at least one such CTest. Tutorial-labeled applications are
guarded by their own matrix and are outside this optimized manifest. Several
rank-count CTests may reuse one binary; the script executes
all invocations for that binary before performing one unambiguous profile
merge/export. The coverage-only executable links the complete static
`rift` archive, and its paired export must list every current `src/*.cpp`
production unit. It explicitly instantiates the reviewed 2D/3D public template
surface in `tests/coverage/supported_templates.txt`; a tooling guard rejects a
missing or unexpected entry before coverage is merged.
After merging, it requires its internal totals, emitted metadata, reparsed LCOV,
and LCOV's own summary to agree exactly. It also prints the path to a `genhtml`
report. LLVM 22 does not export which single outcome of a partially folded
decision is folded, and LCOV 2.0 cannot represent a lone outcome. The helper
therefore validates and reports the complete function-level LLVM totals. It
projects a uniquely identified partial fold out of LCOV and fails if multiple
folded identities remain possible, rather than selecting the identity that
maximizes apparent coverage.

This guarded exact LLVM report is the authoritative 100-percent coverage gate:
it counts distinct physical source lines, canonical source definitions, and
exact canonical authored branch outcomes. The whole-archive executable
prevents a current static-library source unit from escaping its linked
denominator, and the reviewed manifest makes supported 2D/3D template
instantiations explicit. LLVM still cannot prove coverage of an unsupported
or omitted template instantiation. The conservative LCOV projection is
reported separately for interchange and visualization; it is not the branch
gate. After both completeness checks pass, the merger exits nonzero if any
authoritative hit count is smaller than its denominator.

Every invocation creates a fresh profile directory and does not apply gcovr
exclusion markers. The strict raw no-exclusion gcovr workflow in `AGENTS.md`
remains a mandatory published compiler-CFG diagnostic, but it is non-gating.
Run it without unreachable/throw heuristics, ignore-error options, or
fail-under thresholds. Coverage exclusions require their own registered
evidence and user approval; no source exclusion or suppression is currently
approved.

The reviewed Clang 22/gcovr 7.0 strict diagnostic for the T4 evidence tree is
1948/1966 lines (99.1%), 541/543 functions (99.6%), and 1978/3040 compiler-CFG
branches (65.1%). Its 18 missing line records are
`include/rift/discrete_state.hpp:1584,1690,1747,1802,2235,2458,2554`,
`include/rift/mesh_snapshot.hpp:405,548,666`,
`src/discrete_state.cpp:207,215,223,280`, `src/mesh_snapshot.cpp:97,395`, and
`src/phase_graph.cpp:881,1009`. The two missing function records are the
compiler-generated `MeshSnapshotError` copy constructor and the local reader
lambda instantiated in `verify_synchronized_active_fe_index`. Publish a new
inventory after any source change; this reviewed value is context for the raw
diagnostic, not a baseline allowance or exclusion.

For a reviewable raw inventory, add `--json
build/coverage-gcov/strict-no-exclusions.json` to the strict command and run:

```console
python3 scripts/gcovr_miss_inventory.py \
  --report build/coverage-gcov/strict-no-exclusions.json \
  --output-dir docs/development/features/phase-graph-discrete-state-refinement
```

The feature record links the machine-readable
[branch](development/features/phase-graph-discrete-state-refinement/raw-gcovr-branch-misses.tsv),
[line](development/features/phase-graph-discrete-state-refinement/raw-gcovr-line-misses.tsv),
and [function](development/features/phase-graph-discrete-state-refinement/raw-gcovr-function-misses.tsv)
inventories with the exact report and inventory checksums. The branch TSV has
one deterministic row per source location and reports its uncovered and total
compiler-CFG arcs; the sum of its `uncovered` column must equal the raw report's
missing-branch count.

## Build and test

Configure, compile, and run the default debug suite with:

```console
cmake --preset debug
cmake --build --preset debug --parallel 6
ctest --preset debug
```

Also run the release suite when a change affects public interfaces, numerical
code, ownership, or optimization-sensitive behavior. Run `./format.sh` before
submitting a change; it formats `include`, `src`, `tests`, and `tutorials`.

Template-heavy Rift/deal.II builds can exhaust this workstation's memory with
an unbounded job count. Six to eight concurrent jobs are safe; use six by
default and do not exceed eight without explicit approval.

## Documentation

Document every C++ entity, not only the public interface. This includes
private constructors and data members, file-local helpers, and types in the
`rift::detail` namespace. Doxygen extracts these implementation details, and
the documentation build fails when an entity is undocumented.

Use block-form Doxygen comments exclusively:

```cpp
/**
 * Return the phase with the requested stable identity.
 *
 * \param id phase identity to resolve.
 * \return immutable descriptor associated with `id`.
 * \throws std::out_of_range if `id` is not present.
 */
```

Do not use `///` comments. Document template parameters, function parameters,
return values, and thrown exceptions where applicable.

### Explanatory API style

Write API documentation as a structured guide to using the entity. The
`\brief` sentence states what the reader can accomplish. Use the following
extended-description sections when they apply:

- `\par When to use` places the entity in the Rift workflow;
- `\par Typical use` contains a realistic `\code{.cpp}` example;
- `\par Important behavior` explains ownership, lifetime, invariants, and
  performance-relevant behavior; and
- `\par Failure handling` explains errors, exceptions, and invalid inputs.

Every class and struct must contain a complete usage example. The example
should demonstrate its intended workflow using APIs that actually exist; do
not substitute pseudocode or unexplained placeholders. Standalone functions
and important entry points also receive complete examples. Small member
functions use focused snippets and refer back to the class workflow rather
than duplicating the entire example.

Public documentation addresses library users. Documentation for private
members, file-local helpers, and `rift::detail` types addresses maintainers and
explains both the supported internal usage pattern and the invariant it
preserves.

Architectural contracts live in the numbered pages under
`docs/architecture/`. When code or architecture documentation changes,
regenerate the API XML and Sourcey site:

```console
cmake -E make_directory build/doxygen
doxygen Doxyfile
npm run --prefix docs build
```

The rebuilt guide is written to `docs/dist/`.
