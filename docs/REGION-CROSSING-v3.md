# Region Crossing v3

OpenGenesisLINK 7.0.0-dev upgrades adjacent-Region handoff to a two-phase transaction with destination reservation and explicit rollback.

## State machine

```text
prepared
   |
   | reserve
   v
reserved
   |
   | commit + matching reservation token
   v
completed

prepared ---- rollback/expiry ----> rolled_back
reserved ---- rollback/expiry ----> rolled_back

prepared/reserved ---- administrative abort ----> aborted
```

Completed, rolled-back and aborted transactions are terminal.

## Prepare

The authenticated client calls:

```text
POST /v1/viewer/handoff
```

Core validates:

- source and destination Region
- adjacency
- World/Region online state
- moderation policy
- Estate entry policy
- Parcel entry policy

Core then creates a short-lived Crossing record and signs the Crossing ID into the destination Scene Ticket.

### Motion context

Crossing v3 persists:

- destination position
- linear velocity
- rotation
- angular velocity

The handoff request may provide:

- `vx`, `vy`, `vz`
- `rx`, `ry`, `rz`
- `avx`, `avy`, `avz`

All vector components must remain finite. CrossingStore rejects invalid numeric state.

## Runtime context

The Crossing record can carry bounded context for:

- Avatar Appearance and attachments
- owned Script VM state
- physics state
- linkset state

The current avatar handoff path generates physics context from the supplied linear and angular motion. Linkset context is represented by the v3 persistence contract so later object/vehicle crossing work can use the same transaction envelope without another persistence redesign.

The v3 store does not yet claim complete live vehicle/linkset migration between RegionRuntime instances.

## Destination reservation

After the destination Scene accepts the signed Scene Ticket, the client reserves the transaction:

```text
POST /v1/viewer/handoff/reserve
```

Body:

```json
{
  "crossing_id": "<id>",
  "region": "<destination-region>"
}
```

Reservation is bound to:

- authenticated user
- Crossing ID
- destination Region
- unexpired `prepared` state

A successful reservation:

- transitions the record to `reserved`
- records `reserved_unix`
- generates a random 24-byte hexadecimal reservation token

Repeating the same valid reservation is idempotent and returns the existing token. This lets a Viewer retry after losing the HTTP response without creating a second reservation.

## Commit

The final transition is:

```text
POST /v1/viewer/handoff/complete
```

Body:

```json
{
  "crossing_id": "<id>",
  "region": "<destination-region>",
  "reservation_token": "<token>"
}
```

Commit succeeds only when:

- user matches
- destination matches
- Crossing is still unexpired
- state is `reserved`
- reservation token matches in constant time

On success the Crossing becomes `completed`.

Commit-before-reserve is rejected. A wrong token is rejected. A second commit is rejected as replay.

## Rollback

A failed handoff can be explicitly rolled back:

```text
POST /v1/viewer/handoff/rollback
```

Body:

```json
{
  "crossing_id": "<id>",
  "reason": "destination-scene-rejected"
}
```

Rollback is available only for the authenticated Crossing owner while state is `prepared` or `reserved`.

The record stores:

- `rolled_back_unix`
- bounded rollback reason

Retrying rollback on an already rolled-back record is idempotent and returns the existing terminal record.

Completed Crossings cannot be rolled back.

## Expiry

Short-lived prepared or reserved Crossings do not silently disappear when their TTL expires.

Maintenance transitions them to:

```text
rolled_back
rollback_reason = crossing-expired
```

This preserves enough information for diagnostics and audit.

Terminal records are retained for 24 hours, then removed by maintenance.

## Persistence format

CrossingStore v3 persists:

- ID
- user
- source Region
- destination Region
- position
- linear velocity
- rotation
- angular velocity
- state
- created/expires/reserved/completed/rolled-back timestamps
- reservation token
- attachment state
- Script state
- physics state
- linkset state
- rollback reason

Bounded opaque runtime state is Base64 encoded in the line-oriented store.

The loader remains backward-compatible with previous 14-field and 16-field Crossing records.

## Security properties

Crossing v3 adds:

- separate signed Scene Ticket and random reservation credential
- reservation tied to authenticated user and destination
- constant-time reservation-token comparison
- explicit transaction state machine
- commit replay rejection
- idempotent destination reservation
- explicit rollback
- expiry-to-rollback instead of silent deletion
- bounded runtime context
- persisted terminal state for short-term diagnostics

The reservation token is not a replacement for the Scene Ticket. Both protect different parts of the handoff.

## Current boundary

7.0.0-dev materially strengthens the Core transaction, but the Viewer still coordinates the actual network connection switch.

Still open:

- server-to-server object extraction/import
- live linkset root/child recreation in the destination RegionRuntime
- physics-body ownership transfer between World Nodes
- Script runtime ownership migration for object-attached Scripts
- source Region object removal only after destination ACK
- rollback recreation of already removed source objects
- distributed Crossing coordination across multiple Core instances
- seamless socket migration

Those items can now build on the v3 transaction and persistence envelope instead of changing the basic transaction contract again.
