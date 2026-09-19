# Object Crossing v1

OpenGenesisLINK 7.5.0-dev adds the first server-orchestrated migration of a live single Scene object between adjacent Regions.

Object Crossing v1 is separate from Avatar Region Crossing v3. It uses the same transaction-first design principles, but object migration is coordinated over the persistent Core ↔ World Node control plane.

## Goals

The v1 path is designed around four invariants:

1. the source object is not removed before the destination has acknowledged import
2. destination import is idempotent
3. Core accepts commands/results only from the World Node currently owning the relevant Region generation
4. rollback after destination import can reconstruct the source even when a source-remove ACK was lost

## Forward state machine

```text
prepared
   |
   | source export ACK
   v
exported
   |
   | destination import ACK
   v
imported
   |
   | source remove ACK
   v
completed
```

The important ordering guarantee is:

```text
destination import ACK
        before
source object removal
```

Core never sends the source-removal command while the transaction is merely `prepared` or `exported`.

## Rollback state machine

Before destination import, rollback is simple because the source object is still authoritative:

```text
prepared/exported -> rolled_back
```

After destination import, rollback becomes a reconciliation transaction:

```text
imported
   |
   | rollback
   v
cleanup_pending
   |
   | destination cleanup ACK
   v
restore_pending
   |
   | source restore ACK
   v
rolled_back
```

The explicit source-restore step is intentional.

A source World Node can successfully delete the source object and lose its result ACK before Core receives it. In that race Core may still believe the transaction is `imported`. A later rollback therefore cannot assume that the source copy still exists.

The preserved transfer snapshot is used to make source restoration idempotent:

- if the source still exists, restore succeeds without duplicating it
- if the source was already removed, it is reconstructed with the original source entity ID and state

## Persistent ObjectCrossingStore

Core persists every transaction in `ObjectCrossingStore`.

Default path:

```toml
[storage]
object_crossings = "data/object-crossings.db"

[crossing]
object_max_attempts = 5
```

A record contains:

- crossing ID
- owner user ID
- source Region
- destination Region
- source entity ID
- deterministic destination entity ID
- destination position
- serialized transfer snapshot
- state
- per-phase attempt counters
- creation/expiry/export/import/complete/rollback timestamps
- last error
- rollback reason

Terminal records are retained for 24 hours before maintenance removes them.

## Deterministic destination entity ID

The destination object does not reuse the source Region's local entity ID.

Core derives a stable destination entity ID from the Crossing ID and places it in a high-bit transfer namespace.

The same destination ID is reused for command retries. This allows the destination World Node to make import idempotent rather than creating duplicate objects when an ACK is lost.

## Transfer snapshot

Object Crossing v1 transfers:

- source entity ID
- object name
- owner user ID
- group ID
- owner permissions
- group permissions
- everyone permissions
- position
- rotation
- scale
- physical/non-physical state
- linear velocity

Snapshot payload is bounded to 64 KiB before Core accepts it.

Strings that travel inside the bootstrap key/value envelope are Base64 wrapped where required.

## World Node validation

The source World Node independently validates:

- source entity exists
- entity is an object
- object owner matches the authenticated crossing owner

The destination World Node validates:

- transfer snapshot parses correctly
- owner matches the crossing owner
- deterministic destination entity ID is not occupied by an unrelated object
- all numeric state is finite
- destination position is clamped to Region bounds
- destination Z is placed above terrain

Core does not trust the initiating HTTP request as proof that the source object belongs to the user.

## Core ownership validation

For every `object_crossing_poll` and `object_crossing_result`, Core verifies that:

- the Region exists
- the connected World Node owns the Region
- its node generation is current
- the returned command type matches the current transaction state
- the result crossing ID matches the command Core would issue for that Region

Stale World Node generations therefore cannot advance an Object Crossing transaction.

## Core ↔ World messages

Object Crossing v1 adds:

- message 45 — `object_crossing_poll`
- message 46 — `object_crossing_command`
- message 47 — `object_crossing_result`
- message 48 — `object_crossing_result_ack`

Commands are:

- `export`
- `import`
- `remove`
- `cleanup`
- `restore`

The bootstrap payload remains UTF-8 key/value data inside the normal OGL frame.

## API

### Start a crossing

```text
POST /v1/world/object-crossings
Authorization: Bearer <session>
Content-Type: application/json
```

Example:

```json
{
  "source_region": "genesis-central",
  "destination_region": "genesis-east",
  "source_entity_id": 123
}
```

Optional:

```json
{
  "z": 30.0
}
```

Core chooses the edge-side destination X/Y based on Region adjacency.

The request is accepted asynchronously with HTTP 202.

### List own crossings

```text
GET /v1/world/object-crossings
Authorization: Bearer <session>
```

Only records belonging to the authenticated user are returned.

### Roll back

```text
POST /v1/world/object-crossings/rollback
Authorization: Bearer <session>
Content-Type: application/json
```

Example:

```json
{
  "crossing_id": "<id>",
  "reason": "operator-cancelled"
}
```

Completed crossings cannot be rolled back.

## Policy checks before prepare

Core checks:

- source Region exists
- destination Region exists
- Regions are adjacent
- both Regions are online
- destination moderation ban
- destination Estate public/manager access
- destination Parcel build permission

Estate avatar capacity is deliberately not applied to Object Crossing because an object does not consume an avatar slot.

The source World Node still performs the authoritative object-owner check during export.

## Retry and failure behavior

Forward failures have bounded attempt counters.

When export/import repeatedly fails before source removal, Core can terminate the transaction as rolled back while the source remains authoritative.

If source removal repeatedly fails after destination import, Core moves to safety reconciliation rather than declaring success:

```text
cleanup destination -> restore source
```

Cleanup and restore remain retryable because prematurely abandoning either step could leave an unsafe duplicate or lose the authoritative source copy. Their counters saturate at the configured maximum while reconciliation remains pending.

## Expiry

If a transaction expires:

- `prepared` / `exported` -> `rolled_back`
- `imported` -> `cleanup_pending`

This avoids removing the source merely because a transaction timed out.

Safety reconciliation states continue until cleanup/restore succeeds.

## Scene persistence v4

7.5 upgrades `objects.db` writes to Scene persistence v4.

New v4 records add:

- velocity X
- velocity Y
- velocity Z

The loader remains compatible with older 12-field, 13-field and 17-field records.

This means a physical object that crossed successfully retains its linear velocity after a later World Node restart.

## Deliberate v1 boundaries

Object Crossing v1 is a single-object milestone.

Not yet implemented:

- parent/child linkset graph migration
- attachment graph migration
- vehicle-specific state
- angular velocity in the current PhysicsWorld body model
- joints/constraints
- active object-attached Script execution ownership migration
- object Inventory migration as an atomic graph
- distributed coordination between multiple Core instances

Those are follow-up layers on the transaction model rather than reasons to bypass the source-safe ordering established here.
