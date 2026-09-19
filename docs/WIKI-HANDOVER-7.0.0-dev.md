# OpenGenesisLINK Wiki Handover — 7.0.0-dev

> Source material for the OpenGenesisLINK wiki. This document describes the verified 7.0.0-dev server milestone.

## Metadata

- Project: OpenGenesisLINK Server
- Version: `7.0.0-dev`
- Milestone merge commit: `6b23f732c84ef1ea8d414991487e495f006ff745`
- Pull request: `#11`
- Language: C++23
- License: MPL 2.0
- Native federation: OGL-FED/1
- CI: Linux x86_64, Linux ARM64/aarch64, Windows x86_64/MSVC
- Verified PR CI: C++ CI run 120; Windows C++ CI run 91

## Executive summary

7.0 upgrades adjacent-Region handoff from a one-step prepared/completed record into Crossing v3: a persistent two-phase transaction with destination reservation, separate reservation credential, explicit commit, replay rejection and rollback.

The transaction envelope is also expanded so later object, vehicle and attachment migration can use the same persistence contract without another Crossing-store redesign.

## Crossing v3 state machine

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

prepared/reserved ---- abort ----> aborted
```

Terminal states cannot return to a live state.

## Prepare

Endpoint:

```text
POST /v1/viewer/handoff
```

Core verifies adjacency, Region/World availability, moderation, Estate policy and Parcel entry policy before preparing the Crossing.

The Crossing ID remains embedded in the destination Scene Ticket.

## Destination reservation

New endpoint:

```text
POST /v1/viewer/handoff/reserve
```

Reservation verifies:

- authenticated user
- Crossing ID
- destination Region
- unexpired transaction
- reservable state

Successful reservation moves the record to `reserved`, records `reserved_unix` and creates a random 24-byte reservation credential represented as hexadecimal.

Repeating the same valid reservation is idempotent and returns the same reservation token.

## Commit

Endpoint:

```text
POST /v1/viewer/handoff/complete
```

7.0 requires:

- Crossing ID
- destination Region
- reservation token

Commit verifies the token in constant time.

Rejected cases include:

- commit before reserve
- wrong user
- wrong destination
- expired Crossing
- wrong reservation token
- already completed Crossing
- rolled-back/aborted Crossing

A successful commit moves the record to `completed` and records `completed_unix`.

## Rollback

New endpoint:

```text
POST /v1/viewer/handoff/rollback
```

Prepared or reserved transactions can be rolled back by their authenticated owner.

The record persists:

- `rolled_back_unix`
- bounded rollback reason

Repeating rollback on an already rolled-back transaction is idempotent.

Completed transactions cannot be rolled back.

## Expiry behavior

Expired prepared/reserved Crossings are no longer silently deleted.

Maintenance transitions them to:

```text
state = rolled_back
rollback_reason = crossing-expired
```

Terminal records are retained for 24 hours for diagnostics/replay analysis and are then eligible for cleanup.

## Crossing v3 state envelope

Persisted movement state:

- destination position
- linear velocity
- rotation
- angular velocity

Persisted runtime context:

- Avatar Appearance/attachment context
- Script VM context
- physics context
- linkset context
- reservation token
- transaction timestamps
- rollback reason

All runtime context fields are bounded.

## Backward persistence compatibility

The CrossingStore v3 loader continues to read prior persisted 14-field and 16-field Crossing records.

New writes use the v3 format.

This is persistence compatibility for development migration; it is not a frozen stable-file-format guarantee.

## API discovery

7.0 advertises:

- `region-handoff-v2`
- `crossing-v3`
- `crossing-reservation-v1`

New discovery endpoints:

- `viewer_handoff_reserve`
- `viewer_handoff_rollback`

## Security properties

Crossing v3 provides:

- HMAC-signed destination Scene Ticket remains required
- separate random reservation credential
- authenticated user binding
- destination Region binding
- short expiry
- constant-time reservation-token verification
- one-way transaction state machine
- commit replay rejection
- idempotent reserve retry
- explicit rollback
- expiry-to-rollback
- persisted terminal diagnostic state

The Scene Ticket and reservation token serve separate purposes and are both required in the overall handoff flow.

## Test coverage

Unit coverage includes:

- v3 motion/runtime context persistence
- commit-before-reserve rejection
- reserve success
- idempotent reserve
- wrong-token rejection
- successful commit
- commit replay rejection
- reserve followed by rollback
- idempotent rollback
- Scene Ticket still signs the Crossing ID
- Federation runtime Crossing test migrated to reserve/commit contract

Linux process coverage includes three independent smoke paths:

1. full integrated world/social/federation/handoff smoke using reserve/commit
2. Script World query and ACK smoke
3. focused Crossing v3 reserve/rollback smoke

Verified milestone result:

- Linux x86_64: Werror build, all unit tests, all three process smokes PASS
- Linux ARM64/aarch64: Werror build, all unit tests, all three process smokes PASS
- Windows x86_64/MSVC: /WX build and all unit tests PASS

## Current architectural boundary

7.0 establishes the transaction envelope for richer crossings but does not claim complete live object/linkset migration.

Still open:

- server-to-server extraction/import of Scene objects
- destination reconstruction of root/child linksets
- source object deletion only after destination reconstruction ACK
- rollback reconstruction if source removal already occurred
- transfer of active physics-body ownership between World Nodes
- transfer of object-attached Script execution ownership
- full attachment object migration semantics
- distributed Crossing coordination across multiple Core instances
- seamless client socket migration

These can now be developed on top of Crossing v3 without replacing the transaction model.

## Suggested wiki pages

- World / Region Crossing
- World / Handoff Transaction
- Protocol / Crossing v3
- Security / Handoff Credentials
- Runtime / State Migration
- Testing / Crossing v3
- Development Status / 7.0.0-dev

## Related repository documentation

- `README.md`
- `CHANGELOG.md`
- `docs/REGION-CROSSING-v1.md`
- `docs/REGION-CROSSING-v3.md`
- `docs/SCRIPT-WORLD-API-v2.md`
- `docs/OGL-WIRE-FOUNDATION-v0.md`

## Milestone status

```text
OpenGenesisLINK Server 7.0.0-dev
Merge: 6b23f732c84ef1ea8d414991487e495f006ff745
```

7.0.0-dev remains a development build and must not be described as production-stable.
