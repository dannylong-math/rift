# Library reset

## Approved scope

The user approved the six-step reset plan on 2026-09-14 after creating the
backup tag and selecting the new branch. Preserve the existing development
infrastructure and scientific dependencies, remove the foundation implementation
and tutorials, and retain only the public version metadata API. Replace Boost.UT
with Catch2 v3 and keep one executable and CTest entry per test source.

- Working branch: `library-reset` (user selected; continue here).
- Base revision: `400a7f054a79c46280c07d618005863218af3a4e`.
- Backup tag: `reference/pre-reset-status`, verified to resolve to the base.
- Initial working tree: clean.
- Final Git operation: one local reset commit on this branch, as approved in
  the walkthrough. No additional branches, remote Git operations, or publication.
- Reference lookup: `git show reference/pre-reset-status:<path>`.

The user-approved version-only architecture is the API decision gate for this
task. No replacement scientific architecture is being selected. Rift is
pre-release and the repository does not protect source or ABI compatibility;
all active in-repository callers of removed APIs must be removed or updated.
The retained `Version` and `current_version()` API and version 0.1.0 are unchanged.

## Preservation and boundaries

Keep dependency installations, Sourcey configuration/style/lockfile, the general
GCC/Clang CI, Codecov, documentation/Pages, formatting, and dependency cache
workflows. Keep generic MPI test registration available for future tests.
Remove obsolete coverage exclusions along with the implementation they describe.

Ignored `docs/architecture/` and `_planning/` files are user-owned and untouched.
Their pre-change SHA-256 manifest is in the ignored
`build/library-reset/preserved-planning-sha256.json`. Existing tracked foundation
and PDE plans are historical reference, not the active roadmap. Existing
`docs/level_set/` research is preserved.

## Tasks

| ID | Dependency | Owner | Branch | Status | Acceptance evidence |
|---|---|---|---|---|---|
| R1 | Approved scope | reset_implementation | library-reset | Complete | Debug/Release/tidy/sanitizers and both coverage scripts pass; `build/library-reset/implementation-handoff.md` |
| R2 | Approved scope; R1 before docs build | reset_docs | library-reset | Complete | Doxygen, npm ci, Sourcey passed; `build/library-reset/docs.log` |
| R3 | R1 | reset_skeptic | library-reset | Complete | [Test audit](test-audit.md); five selected faults detected, seven coverage fixtures checked |
| R4 | R1–R3 | reset_final_audit | library-reset | Complete | [Quality audit](quality-audit.md): READY FOR HUMAN REVIEW |
| R5 | R4 | Primary manager | library-reset | Complete in delivery commit | [Reset report](reset-report.md); one local commit containing these records |

Owners edit disjoint files. The implementation owner controls all source, tests,
CMake, coverage-script changes, and necessary CI fixes. The documentation owner
controls user guidance. The manager controls this directory and local Git.
No specialist creates commits; the final commit records the integrated result.

## Acceptance and reproducibility

Run Debug (including configured ASan/UBSan), Release, GCC coverage, Clang coverage,
clang-tidy where available, formatting, Doxygen, and Sourcey. Require a nonempty
first-party line/function denominator and no uncovered in-scope code. A zero
branch denominator is explicitly reported as no branches, not evidence of branch
testing. Preserve raw and policy-adjusted metrics; no old exclusions carry over.
Check the coverage gate rejects incomplete/empty reports. Check historical tag
access and preservation of user planning documents. Maximum six concurrent build
jobs. Record exact commands and tool versions in the final report.

There is no numerical method, physical model, solver, or scientific calculation
left in the reset surface. Numerical verification, model validation, uncertainty,
and performance benchmarking are not applicable. Software verification covers
the version API and development infrastructure; it does not validate future
scientific functionality.

## Implementation decisions

- Catch2 3.16.0 is pinned at commit
  `317ac1ed4c0bb6e6b91eafc817e05c488feffcb3`; the implementation owner verified
  its release tag against upstream. Only `*_test.cpp` files are test targets.
- Ordinary tests use `Catch2::Catch2WithMain`; future MPI test executables use
  `Catch2::Catch2` and an MPI-aware main. No MPI-dependent test remains today.
- Remove broad GCC throw/unreachable/noncode suppression flags as well as the
  exact historical Clang exclusions. The baseline has no exclusions.
- Preserve `noexcept` through a compile-time test as well as checking the three
  independent expected version components at runtime.
- Documentation regeneration clears obsolete generated Doxygen XML before
  rebuilding, avoiding old API pages in the new site.
- Catch2 is a compiled third-party dependency. Disable clang-tidy on its two
  implementation targets while retaining analysis on Rift and its tests.
