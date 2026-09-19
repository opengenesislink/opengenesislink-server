# Object Crossing v2

OpenGenesisLINK 8.0.0-dev upgrades Object Crossing from a single-object transaction to a bounded linked-object migration protocol.

## Core invariant

The safety ordering remains:

```text
source export
    ->
destination import ACK
    ->
source remove
    ->
script binding commit
```

The source linkset is not removed before destination reconstruction has succeeded.

## Linkset snapshot

A v2 snapshot contains:

- source root entity ID
- up to 64 object members
- source entity ID per member
- parent/root relation and link number
- name
- owner/group and permission masks
- position/rotation/scale
- physical flag
- linear velocity
- angular velocity
- floating text

The serialized snapshot is bounded to 256 KiB.

## Destination IDs

The destination root uses the deterministic crossing transfer ID.

Children receive deterministic transfer IDs derived from:

- destination root ID
- source member ID
- link number

Destination import returns a bounded source-to-destination entity map:

```text
source:destination,source:destination,...
```

Core persists that map with the crossing transaction so it survives restart.

## Script ownership handoff

Scripts retain their Script IDs and VM state.

Their object binding is stored as:

```text
region-id/entity-id
```

After the World Node confirms source removal, Core atomically rebinds scripts belonging to the crossing owner from each source entity to the corresponding destination entity.

The operation is idempotent, so a lost Core/World result acknowledgement does not create a second migration.

Scripts are deliberately not rebound immediately after destination import. Until commit, the source remains authoritative.

## Rollback and reconciliation

The v2 rollback path is:

```text
imported/exported
    ->
cleanup_pending
    ->
restore_pending
    ->
rolled_back
```

Unlike v1, an `exported` transaction is not blindly declared rolled back.

Reason: the destination may have imported successfully while its ACK was lost. Core therefore issues idempotent destination cleanup before source restoration.

This rule also applies when an exported transaction expires.

## Source restoration

The preserved linkset snapshot can be imported with original source entity IDs.

If the source still exists, restoration is idempotent.

If source deletion succeeded but its ACK was lost, the complete root/child graph can be reconstructed with the original local IDs.

## Persistence

ObjectCrossingStore v2 persists:

- v1 transaction fields
- larger linkset snapshot
- source-to-destination entity map

The loader remains compatible with v1 records.

## Deliberate boundaries

v2 does not yet migrate:

- object Inventory contents as an atomic graph
- object-attached asset ownership changes
- physics joints/constraints
- avatar attachment graphs
- multi-Core distributed transaction consensus

Those layers should build on the same destination-first commit ordering rather than bypassing it.
