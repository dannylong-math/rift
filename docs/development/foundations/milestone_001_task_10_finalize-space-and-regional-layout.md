# Milestone 001 / Task 10: Finalize space and regional layout

| Field | Value |
|---|---|
| Status | Complete |
| Owner | Pair |
| Estimated user effort | About one working day |
| Depends on | Task 09 |
| Allowed files/modules | New `include/rift/space_snapshot.hpp`, `src/space_snapshot.cpp`, and private `src/field_space_storage.hpp`; creator/transfer changes in the existing draft and context modules; build registration; focused serial `tests/space_snapshot_test.cpp` and MPI `tests/mpi/space_snapshot_agreement_test.cpp` |
| Public behavior | Canonical regional scalars, complete representation-neutral immutable layout, and a published `SpaceSnapshot` |
| API/ABI | Pre-release new layout/snapshot lookup API; finalization invalidates its draft |

## Goal

Complete the continuous-field, optional discrete geometry-metadata, and regional
scalar layout; assign canonical offsets/identities; publish an immutable
`SpaceSnapshot`; and make the originating draft unusable so no partial or
conflicting layout can escape. Here “regional scalar” means a nonspatial unknown
associated with a connected-region constraint, not the phase labels used by a
regional level-set representation.

## Context and interaction

```mermaid
flowchart LR
    DRAFT["active SpaceDraft"] --> REG["validate + sort regional schema"]
    REG --> OFFSETS["field/regional prefix offsets"]
    OFFSETS --> LAYOUT["complete StateLayout"]
    LAYOUT --> SNAP["immutable SpaceSnapshot"]
    SNAP --> INVALIDATE["draft inactive"]
```

## Approved API sketch

```cpp
struct RegionalEntrySpecification {
  PhaseId phase;
  std::string name;
};

using RegionalEntryId = StrongId<detail::RegionalEntryIdTag>;

struct PhaseSupportFieldLayoutEntry {
  PhaseSupportFieldGroupId field_group;
  std::uint64_t offset;
  std::uint64_t cardinality;
};

struct GeometryFieldLayoutEntry {
  GeometryFieldGroupId field_group;
  std::uint64_t offset;
  std::uint64_t cardinality;
};

struct DiscreteGeometryMetadataLayoutEntry {
  DiscreteGeometryMetadataId metadata;
  std::uint64_t offset;
  std::uint64_t cardinality;
};

struct RegionalEntry {
  RegionalEntryId id;
  PhaseId phase;
  std::string name;
  unsigned int owner_rank;
  std::uint64_t offset;
  std::uint64_t cardinality;
};

class StateLayout {
public:
  std::span<const PhaseSupportFieldLayoutEntry>
  phase_support_fields() const noexcept;
  std::span<const GeometryFieldLayoutEntry>
  geometry_fields() const noexcept;
  std::span<const DiscreteGeometryMetadataLayoutEntry>
  discrete_geometry_metadata() const noexcept;
  std::span<const RegionalEntry> regional_entries() const noexcept;
  std::uint64_t total_cardinality() const noexcept;

  const PhaseSupportFieldLayoutEntry&
  phase_support_field(PhaseSupportFieldGroupId) const;
  const GeometryFieldLayoutEntry&
  geometry_field(GeometryFieldGroupId) const;
  const DiscreteGeometryMetadataLayoutEntry&
  discrete_geometry_metadata(DiscreteGeometryMetadataId) const;
  const RegionalEntry& regional_entry(RegionalEntryId) const;
};

template<int dim> class SpaceSnapshot {
public:
  ~SpaceSnapshot();
  SpaceSnapshot(const SpaceSnapshot&) = delete;
  SpaceSnapshot& operator=(const SpaceSnapshot&) = delete;
  SpaceSnapshot(SpaceSnapshot&&) = delete;
  SpaceSnapshot& operator=(SpaceSnapshot&&) = delete;

  SpaceEpoch epoch() const noexcept;
  const SpaceSchema& canonical_schema() const noexcept;
  const PhaseSupportSet<dim>& phase_supports() const noexcept;
  const StateLayout& state_layout() const noexcept;

  std::span<const PhaseSupportFieldGroupSpace<dim>>
  phase_support_field_spaces() const noexcept;
  std::span<const GeometryFieldGroupSpace<dim>>
  geometry_field_spaces() const noexcept;
  const PhaseSupportFieldGroupSpace<dim>&
  phase_support_field_space(PhaseSupportFieldGroupId) const;
  const GeometryFieldGroupSpace<dim>&
  geometry_field_space(GeometryFieldGroupId) const;

  std::optional<PhaseSupportFieldGroupId>
  find_phase_support_field(PhaseId, std::string_view) const noexcept;
  std::optional<GeometryFieldGroupId>
  find_geometry_field(std::string_view) const noexcept;
  std::optional<DiscreteGeometryMetadataId>
  find_discrete_geometry_metadata(std::string_view) const noexcept;
  std::optional<RegionalEntryId>
  find_regional_entry(PhaseId, std::string_view) const noexcept;
};

enum class SpaceFinalizationErrorCode : std::uint8_t {
  inactive_draft,
  foreign_draft,
  field_spaces_not_built,
  empty_name,
  invalid_name_encoding,
  duplicate_name,
  unknown_phase,
  collective_space_epoch_mismatch,
  collective_draft_state_mismatch,
  regional_schema_mismatch,
  layout_overflow,
  layout_mismatch
};

struct RegionalEntryErrorSubject {
  std::size_t sorted_index;
  RegionalEntrySpecification specification;
};

enum class LayoutEntryKind : std::uint8_t {
  phase_support_field,
  geometry_field,
  discrete_geometry_metadata,
  regional_scalar
};

struct LayoutEntryErrorSubject {
  LayoutEntryKind kind;
  std::size_t canonical_index;
  std::string name;
  std::optional<PhaseId> phase;
};

using SpaceFinalizationErrorSubject =
    std::variant<std::monostate, RegionalEntryErrorSubject,
                 LayoutEntryErrorSubject>;

struct SpaceFinalizationError {
  SpaceFinalizationErrorCode code;
  unsigned int rank;
  SpaceFinalizationErrorSubject subject;
  std::string message;
};

using SpaceFinalizationErrors = std::vector<SpaceFinalizationError>;

template<int dim>
using SpaceSnapshotResult =
    std::expected<std::shared_ptr<const SpaceSnapshot<dim>>,
                  SpaceFinalizationErrors>;

// Collective transactional finalization boundary:
// RiftContext::finalize_space(
//     SpaceDraft<dim>&,
//     std::vector<RegionalEntrySpecification>);
```

Exact lookup and result types are approved before implementation.

Regional entries are keyed canonically by `(PhaseId, name)`. Names must be
nonempty, valid UTF-8, and unique within one phase; the same semantic name may
therefore be used by different phases. The name is an opaque semantic key
supplied by the later regional-constraint system. Milestone 001 does not invent
a connected-region identity before the geometry service can author and verify
one.

`RiftContext::finalize_space(...)` is the collective publication boundary. It
accepts the draft as an lvalue so recoverable validation or agreement failures
can leave it active for a corrected retry. Only complete successful
publication deactivates the draft and transfers its owned support and field
spaces into the immutable snapshot.

A successful result is a `std::shared_ptr<const SpaceSnapshot<dim>>`. This
matches immutable mesh-snapshot ownership and lets state, geometry, and
operator services retain the exact published space while its mesh, finite-
element objects, DoF handlers, and constraints remain alive.

Each draft also retains a private non-owning stamp naming the `RiftContext`
that created it. Finalization compares that stamp with `this` and collectively
rejects a draft from another context without adding a public context or run
identity. Moving a draft transfers the stamp. The existing lifetime contract
still requires every draft and snapshot to be destroyed before its non-movable
context.

An older active draft is not stale merely because the context has created a
newer epoch. Task 08 permits multiple reusable draft-creation calls. Task 10
uses “stale” only for cross-rank epoch disagreement or inconsistent draft
lifecycle/build state.

Finalization returns a dedicated collected `SpaceFinalizationErrors` rather
than extending draft-construction diagnostics or throwing for recoverable
configuration defects. It reports lifecycle, regional-input, cross-rank
agreement, and checked-layout-arithmetic failures with the codes shown above.
As in Tasks 08 and 09, each independently detected defect is one atomic error,
and complete errors are gathered only on failure.

Regional-input errors retain the complete offending specification and its
position after canonical sorting. Layout-arithmetic errors retain a typed
category, canonical index, name, and optional phase, so diagnostics can name
the block without parsing text or requiring a snapshot that was never
published. Lifecycle and whole-rank agreement errors use `std::monostate`.
Every error also carries the offending world rank and an immediately usable
message, following the existing diagnostic pattern.

`StateLayout` assigns one flat semantic sequence in this order: canonical
phase-support field groups, canonical geometry field groups, canonical
discrete geometry metadata, then canonical regional scalars. Every entry has a
checked prefix offset and cardinality, and the layout publishes their checked
total. These are logical state ordinals used for deterministic identity,
inspection, and later storage planning; they do not require one monolithic
vector. Each continuous field retains its independent deal.II DoF numbering,
and discrete metadata counts only the DoFs of its referenced scalar geometry
component.

Regional entries are optional model-provided state. A conventional fully
compressible phase has spatial pressure in a continuous field group and need
not request any regional scalar; a closed general-EOS low-Mach region may, for
example, request one spatially uniform thermodynamic-pressure entry. Other
models may request regional entries only when their equations actually contain
such nonspatial unknowns.

Each canonical regional entry records one authoritative world rank selected by
`RegionalEntryId::value() % n_mpi_processes()`. This round-robin placement is
deterministic and balances entry counts without depending on mutable cell
partitioning or the deferred connected-region geometry. Task 10 records the
owner only; Tasks 11 and 14 provide owner-side storage and exact replicated
access.

`StateLayout` publishes separate immutable entry types for phase-support
fields, geometry fields, discrete geometry metadata, and regional scalars.
This preserves category-specific strong identities and metadata without a
generic variant or optional members on every entry.

Continuous-field and discrete-metadata layout entries retain only their
category-specific strong ID plus offset and cardinality; the owning
`SpaceSnapshot` resolves that ID against its one canonical schema and field-
space collection. They do not create a third copy of field names, component
bindings, or descriptors and do not keep cross-member pointers. `RegionalEntry`
owns its phase, name, and owner metadata because it is itself the canonical
regional descriptor; its Milestone 001 cardinality is always one.

Name-based discovery lives on `SpaceSnapshot`: it returns `std::optional` and
requires a `PhaseId` for phase-support fields and regional entries. Supplying a
wrong phase therefore produces no match. `StateLayout` exposes canonical const
spans and direct strong-ID access; an invalid ID throws `std::out_of_range`.
Once discovered, phase ownership remains inspectable through the corresponding
field descriptor or regional entry, so direct ID access does not redundantly
accept a phase.

On success, `SpaceSnapshot<dim>` becomes the sole owner of the draft's
`PhaseSupportSet`, `SpaceSchema`, and field-space storage together with the new
`StateLayout`. It exposes only the const views and lookup operations shown
above. The snapshot object is non-copyable and non-movable at its stable shared
allocation; callers copy or move only `std::shared_ptr<const
SpaceSnapshot<dim>>`. Its mesh remains available through
`phase_supports().mesh_snapshot()` rather than a duplicate member.

Collective finalization forms one local candidate packet containing dimension,
draft epoch and state, canonical regional specifications, every canonical
layout tuple, regional owners, and total cardinality whenever local validation
permits. Rank zero broadcasts its complete packet, each rank compares exact
packed bytes, and one fixed-size all-reduce establishes global validity and
agreement. A failure all-gathers complete typed diagnostics; success publishes
directly with no post-publication collective. Thus the normal path adds one
variable-size broadcast and one fixed-size reduction rather than separate
regional-schema and layout rounds.

For discrete geometry metadata, Task 10 publishes the referenced scalar
component through its canonical descriptor and records only the component's
logical cardinality. Because every current geometry space is an `FESystem` of
identical primitive `FE_Q` components, that cardinality is exactly the field
space's global DoF count divided by its component count, without another MPI
reduction. Task 11 constructs the compact owner/ghost storage and its mapping
to the target component's potentially sparse original DoF indices. Task 10
does not allocate a full field-sized metadata vector or retain allocation-
specific mapping tables.

Finalized regional entries, `StateLayout`, result/diagnostic vocabulary, and
`SpaceSnapshot<dim>` live in the dedicated public
`include/rift/space_snapshot.hpp` module with implementation in
`src/space_snapshot.cpp`. Draft schema and lifecycle remain in
`space_draft.hpp`. The two implementation files share only the private
`src/field_space_storage.hpp` owner definition, which is not installed or
exposed as public API. `rift_context.hpp` includes the new snapshot module for
the collective finalization member.

## Work contract

1. Validate and canonically order regional entries, including ownership and
   duplicate-name rules.
2. Compute complete continuous-field blocks, optional discrete geometry-
   metadata blocks aligned with their referenced scalar component's DoF
   layout, and regional-scalar offsets/cardinalities with checked arithmetic
   and an independent layout oracle.
3. Publish immutable layout and space views that retain their field spaces.
4. Make finalization single-use and reject foreign, stale, or inactive drafts.
5. Agree on exact published layout across world ranks.
6. Test permutations, offsets, cardinalities, lookups, invalid drafts, and
   collective agreement; add Doxygen and stop.

### Non-goals

- Allocating state vectors or assigning physical values.
- Regional equations/constraints, geometry occupancy or reconstruction, or mutation after
  publication.
- Rebuilding spaces during a nonlinear attempt.

## Focused tests

| Behavior/property | Independent oracle | Important cases |
|---|---|---|
| Canonical regional schema | Independent sorted tuple list | Empty, several, duplicates, permutations |
| Layout offsets | Hand-computed block sizes, prefix sums, and totals | Scalar/multicomponent fields, optional discrete geometry metadata, and regional scalars |
| Single-use finalization | Draft state model | Success, repeated finalize, foreign/stale draft |
| Immutable lookup | Expected ID/name/block maps | Missing field/region and wrong phase |
| Rank agreement | Gather serialized layout tuples | Equivalent permuted input; intentional mismatch |

## Documentation

- Define layout ordering, offsets/cardinalities, geometry-metadata ownership,
  regional-scalar ownership, the distinction between regional scalars and a
  regional geometry representation, `SpaceEpoch` scope, snapshot immutability,
  and draft invalidation.

## Completion evidence

- [x] Requested artifact or behavior exists
- [x] Focused tests pass
- [x] Relevant regression tests pass
- [x] Task-level coverage was measured when useful
- [x] Documentation requested for this task is complete
- [x] Commands and results are recorded below

## Evidence and handoff

- Commands/results:
  - `./format.sh`: passed.
  - `cmake --build --preset debug --parallel 6`: passed.
  - `ctest --preset debug --output-on-failure`: all 40 serial and MPI tests
    passed, including the Task 10 executable at one, two, and three ranks.
  - `cmake --preset debug-tidy` and
    `cmake --build --preset debug-tidy --parallel 6`: passed with no
    clang-tidy warnings.
  - `ctest --preset debug-tidy --output-on-failure`: all 40 tests passed under
    the configured sanitizer build.
  - `./scripts/run_coverage.sh gcc`: all 40 tests passed; raw coverage was
    99.3% lines (2708/2727), 100% functions (438/438), and 64.6% branches
    (1949/3016); policy-adjusted coverage was 100% lines (2708/2708),
    functions (438/438), and branches (1890/1890).
  - `./scripts/run_coverage.sh clang`: all 40 tests passed; raw coverage was
    98.60% lines (2603/2640), 100% functions (312/312), and 98.39% branches
    (732/744); policy-adjusted coverage was 100% lines (2603/2603), functions
    (312/312), and branches (732/732). `llvm-cov` retained the known
    multi-executable merge warning that 64 functions have mismatched data; the
    exact uncovered-line and branch manifest still matched.
  - `doxygen Doxyfile`: passed with no reported warnings.
  - `npm run --prefix docs build`: passed; Sourcey built 101 pages.
- Approved Task 10 coverage exclusions:
  - `src/space_snapshot.cpp:258-263`: the foreign-context defensive branch;
    deal.II permits only one live `MPI_InitFinalize`, and the non-movable
    `RiftContext` is its public lifetime owner.
  - `src/space_snapshot.cpp:333-341`: aggregate layout cardinality beyond
    `std::uint64_t`, which cannot be materialized by supported process
    resources.
  - `src/space_snapshot.cpp:422-429`: more than 2^32 owned regional
    specifications in one process, which cannot be materialized by supported
    process resources.
  - `src/space_snapshot.cpp:613-643`: failure-only wire conversions for typed
    layout subjects, reachable only from the two excluded overflow guards.
  GCOVR suppressions are adjacent to these blocks, and Clang coverage checks
  their exact line and branch locations so an unrelated future miss fails.
- Files changed: added the public snapshot/layout API, its implementation and
  private transferred field-space storage; extended `RiftContext` and
  `SpaceDraft` for transactional publication; registered the implementation;
  added serial and 1/2/3-rank MPI tests; updated the exact coverage-exclusion
  manifest and the Task 10 planning records.
- Risks/deferred work: Task 10 proves exact distributed layout agreement but
  deliberately does not allocate state vectors, build compact discrete-
  metadata maps, or implement regional equations. Those remain Task 11 and
  later work.
- Next stopping point: Task 10 is complete. Stop before Task 11 state
  allocation.
