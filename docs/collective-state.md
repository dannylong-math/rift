---
title: Collective state transactions
description: Build state stores, edit owner-local fields, and publish immutable snapshots safely across MPI ranks.
---

# Collective state transactions

[Tutorial 3](tutorials/tutorial-003.md) shows the complete practical workflow
on two MPI ranks: create a layout, edit owner-local vectors, update regional
state, seal a candidate, and publish it. This page is the deeper transition and
retention reference.

A finalized `StateLayout` describes vector meaning and ownership. A
`StateStore` supplies the changing values for exactly that immutable layout.
Create the store collectively and keep all later transitions in the same order
on every rank:

```cpp
#include <rift/discrete_state.hpp>

rift::StateTransitionResult<rift::StateStore>
create_state(const rift::StateLayout& layout) {
  return rift::make_state_store(
      layout,
      rift::StateRetentionPolicy{.max_pinned_private_snapshots = 0});
}
```

The factory first compares the complete layout and retention policy on the
retained run communicator. Only after that agreement does it use the mesh
communicator and initialize native distributed vectors. The returned accepted
snapshot is immutable and zero initialized. Each vector owns exactly the
layout's locally owned DoFs; the state API makes no ghost-value promise.

## Edit privately, then publish

Use a provenance-bearing `StateFieldReference` obtained from the layout. A
bare numeric field ID is deliberately insufficient because the same number can
name a different field in another space generation.

```cpp
#include <rift/discrete_state.hpp>

#include <utility>

rift::StateTransitionResult<rift::StateSnapshot>
publish_one(rift::StateStore& store,
            const rift::StateFieldReference field) {
  const auto base = store.snapshot(rift::StateSlot::accepted);
  auto trial_result =
      store.begin_trial_collective(base.stamp().snapshot);
  if (!trial_result)
    return std::unexpected(trial_result.error());

  auto trial = std::move(*trial_result);
  auto vector_result = trial.field(field);
  if (!vector_result)
    return std::unexpected(vector_result.error());
  vector_result->get() = 1.0;

  auto candidate_result = trial.seal_collective();
  if (!candidate_result)
    return std::unexpected(candidate_result.error());
  return store.publish_collective(candidate_result->stamp().snapshot);
}
```

`begin_trial_collective()` makes a private copy of any retained base.
`seal_collective()` freezes that copy as an immutable private candidate.
`publish_collective()` accepts only a candidate descending from the current
accepted root. Publishing one sibling therefore makes another sibling stale.
Use `discard_collective()` to remove the current unpinned transient candidate
without changing accepted state.

Successful publication preserves the candidate's `StateSnapshotId`, assigns a
new `StateEpoch`, and moves the old accepted state to `previous`. Logical
rejections leave the store and transaction unchanged and consume no IDs. An
atomic identity reservation advances none of its store, transaction, snapshot,
epoch, or level-set components if any selected component is exhausted.

## Bound private retention explicitly

The store lookup owns exactly the accepted snapshot, an optional immediately
previous publication, at most one unpinned transient private snapshot, and up
to `max_pinned_private_snapshots` pinned private snapshots. It enforces this
bound without adding a constant to the configured capacity, so `SIZE_MAX` is a
valid effectively unlimited pin setting.

Each successful seal makes its candidate the transient and evicts the former
transient lookup entry. Pin a candidate before sealing another candidate when
both IDs must remain usable:

```cpp
auto first_trial = store.begin_trial_collective(base_id).value();
auto first = first_trial.seal_collective().value();
if (auto pinned = store.pin_collective(first.stamp().snapshot); !pinned)
  return std::unexpected(pinned.error());

auto second_trial = store.begin_trial_collective(base_id).value();
auto second = second_trial.seal_collective().value();
```

Pinning the transient consumes one explicit slot and is idempotent for an
already pinned candidate. A new pin at capacity returns `pin_limit_reached`
without evicting anything; capacity zero therefore still permits
seal-then-publish. `unpin_collective()` is idempotent for the transient. For a
pinned candidate, it immediately makes that candidate transient, frees its pin
slot, and evicts the former transient.

Publishing either kind of private candidate preserves its snapshot ID and
removes its pin/transient state. A different transient sibling stays registered
and becomes stale. Stale private snapshots may be pinned for inspection, but
publication still returns `stale_accepted_root`. Discard succeeds only for the
transient; pinned, accepted, and previous snapshots return `invalid_discard`.
Pin or unpin on accepted or previous state returns `wrong_candidate_state`.

Active transactions retain their copied base data, but do not protect the base
ID from eviction and consume no pin capacity. An already active transaction can
seal after its base ID leaves the lookup; a new begin call for that ID returns
`unknown_snapshot`. External immutable handles remain readable after every
lookup removal and after store destruction.

## Collective errors and lifetimes

The first transition descriptor contains the operation, store, transaction or
snapshot argument, and relevant state flags. Rift compares that descriptor
before mutation, so operation, store, transaction, argument, expired-authority,
inactive-transaction, snapshot-state, and stale-root errors have deterministic
precedence and are identical on every participating rank.

The local `snapshot(StateSlot::previous)` lookup throws `std::out_of_range`
until a publication creates a previous slot. The identity overload throws the
same exception for an unknown or evicted ID. Collective methods instead return
fixed-size errors: factory layout/policy mismatches and exhaustion; begin
operation/store/argument, unknown-base, and exhaustion errors; seal
operation/store/transaction, expired/inactive, and exhaustion errors; publish
operation/store/argument, unknown/wrong-state/stale-root, and exhaustion
errors; pin operation/store/argument, unknown/wrong-state/pin-limit errors;
unpin operation/store/argument or unknown/wrong-state errors; and discard
operation/store/argument, unknown, or invalid-state errors.

`StateTransitionError` contains only a fixed-size code. Its diagnostic is a
static `string_view`, so returning a logical error cannot allocate
asymmetrically. MPI failures, dependency failures, and rank-local allocation
failures after collective agreement invoke the retained run fatal handler;
peers cannot safely rejoin at different protocol stages.

Snapshots retain immutable storage, mesh lifetime, and collective context.
They remain readable after the originating store handle is destroyed or after
the store evicts their lookup entry. Transactions retain their base snapshot
strongly but the mutable store authority weakly. Sealed, abandoned, moved-from,
and expired-store transactions retain a collective tombstone, allowing
`seal_collective()` to return the same inactive or expired error on every rank.
Move construction transfers a store handle without replacing its shared
authority: a transaction begun through the source handle remains valid and is
sealed or published through the destination handle. The moved-from store is
valid only for destruction; do not call its state API.
Calls that mutate one store, and calls across stores sharing a communicator,
must be externally serialized.

## Exact regional scalars and level-set revisions

Regional entries use semantic scalar access instead of exposing their
rank-zero-only backend vectors. Set a transaction-private entry collectively,
then read the exact synchronized representation locally from any immutable
snapshot:

```cpp
const auto entry = layout.regional_entries().front().id;
auto trial = store.begin_trial_collective(base_id).value();
if (auto changed = trial.set_regional_value_collective(entry, -0.0); !changed)
  return std::unexpected(changed.error());
auto candidate = trial.seal_collective().value();
const double value = candidate.regional_value(entry);
```

Every mesh-communicator rank must call the setter in the same externally
serialized transition order with the same transaction, entry, and exact
IEEE-754 binary64 value bits. Entry or value-bit divergence returns
`argument_mismatch`; nonfinite values are accepted. Rejection changes neither
the backend nor the replicated cache and reserves no identity. Success updates
the backend on mesh-communicator rank zero and the cache on every rank. The
local getter performs no communication and throws `std::out_of_range` for an
entry outside the snapshot's layout.

Sealing synchronizes the owner backend representations into every cache before
freezing the candidate. Regional-only changes preserve
`LevelSetFieldSetSnapshotId`. Every owner-local level-set `double` is compared
with its base by exact object representation and the boolean result is reduced
collectively. Thus `+0.0` and `-0.0` differ; an identical infinity or NaN
payload is unchanged; and any changed sign, exponent, or payload bit creates a
new revision. Nonfinite level-set values remain representable—the revision
test does not claim physical admissibility. Exhausting a required level-set
identity leaves the active transaction and all five identity sequences
unchanged for a corrected retry.
