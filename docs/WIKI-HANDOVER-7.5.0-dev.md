# Wiki Handover — OpenGenesisLINK Server 7.5.0-dev

## Milestone

OpenGenesisLINK Server 7.5.0-dev introduces the first server-orchestrated live Scene-object migration between adjacent Regions.

Milestone merge:

- Pull request: #12
- squash merge: `f742ba19a040e169c82e02cccf833f7aca296f94`
- required CI: Linux x86_64, Linux ARM64/aarch64 and Windows x86_64
- Linux additionally runs the full Object Crossing process smoke

## Object Crossing v1

Successful transaction:

```text
prepared -> exported -> imported -> completed
```

Command order:

```text
source export -> destination import ACK -> source remove
```

The source object is not removed before the destination has acknowledged a successful import.

Rollback after destination import:

```text
imported -> cleanup_pending -> restore_pending -> rolled_back
```

The explicit restore phase closes the lost source-remove ACK race. A rollback first removes the destination copy and then reconstructs the source from the preserved transfer snapshot if necessary.

## Transfer state

A v1 transfer snapshot carries:

- name
- owner
- group
- owner/group/everyone permission masks
- transform
- physical state
- linear velocity

Destination entity IDs are deterministic per transaction so import retries are idempotent.

## Core / World protocol

Added control messages:

- `object_crossing_poll` (45)
- `object_crossing_command` (46)
- `object_crossing_result` (47)
- `object_crossing_result_ack` (48)

Core verifies Region ownership and current World Node generation before it accepts poll/result traffic.

## API

Authenticated endpoints:

```text
GET  /v1/world/object-crossings
POST /v1/world/object-crossings
POST /v1/world/object-crossings/rollback
```

Preparation validates adjacency, online state, moderation, Estate object access and destination Parcel build policy. Object ownership is revalidated by the source World Node.

## Scene persistence v4

Scene object records now retain linear velocity for physical objects.

The loader remains compatible with earlier 12-field, 13-field and 17-field records. v4 uses 20 fields.

## HTTP correction discovered by E2E testing

The new process smoke exposed that the common HTTP status renderer did not know `202 Accepted`; successful asynchronous responses were therefore rendered as HTTP 500 despite carrying a correct JSON body.

The shared HTTP layer now explicitly supports:

- 202 Accepted
- 204 No Content

This was fixed centrally rather than worked around in Object Crossing.

## Test evidence

The final exact PR head `ccd2598cc1f419d2adeca92e81dfa0be2e8e42cc` passed:

- Linux x86_64 warnings-as-errors build
- Linux x86_64 unit tests
- Linux x86_64 integrated process smoke
- Linux x86_64 Script World query/ACK smoke
- Linux x86_64 Crossing v3 reserve/rollback smoke
- Linux x86_64 Object Crossing E2E smoke
- Linux ARM64/aarch64 equivalent build/tests/smokes
- Windows x86_64 MSVC /WX build
- Windows unit tests

## Deliberate limits

7.5 does not claim:

- linkset graph migration
- attachment graph migration
- angular velocity in the live PhysicsWorld body model
- vehicle constraints/joints
- object Inventory graph migration
- object-attached Script binding migration
- multi-Core distributed transaction coordination

These are follow-up areas for the Object Runtime / Linkset Crossing milestone.

## Recommended next milestone

8.0.0-dev should evolve the v1 single-object path into a broader native Object Runtime:

- real linkset parent/child relationships
- angular motion in PhysicsWorld
- richer Scene object operations
- Script World API v3
- linkset-aware Object Crossing
- Script object-binding migration
- stronger timeout reconciliation for lost destination import ACKs
- Scene persistence v5
- CI dependency caching
