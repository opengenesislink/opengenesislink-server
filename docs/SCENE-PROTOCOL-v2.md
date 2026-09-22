# OpenGenesisLINK Scene Protocol v2

> Release note: Server 1.0.0-alpha.1 freezes the named `scene-v2` application contract as the first Viewer-development baseline. Additive capabilities may be introduced; breaking changes require a new Scene contract revision.

OpenGenesisLINK 13.0.0-dev defines the second native Viewer-facing Scene contract.

The binary transport remains the existing OGL1 framed protocol with protocol version 1. Scene v2 is an application-level contract layered on that transport.

## Session flow

1. Authenticate against Core.
2. Request `POST /v1/viewer/bootstrap` or `POST /v1/viewer/session`.
3. Core returns a short-lived signed Scene Ticket.
4. Connect to the World Scene endpoint.
5. Send `HELLO`.
6. Send `SCENE_JOIN` with Region id and Scene Ticket.
7. Use Scene v2 synchronization, metadata and movement messages.

The Scene Ticket remains bound to user, Region, expiry, groups and capabilities.

## HELLO contract

The Scene server advertises:

- `scene_contract=2`
- `sync=scene-sync-v1`
- `movement=avatar-reconcile-v1`
- `metadata=region-metadata-v1,parcel-read-v1`
- `capabilities=scene-capabilities-v2`

The transport itself still reports `protocol=1`.

## Scene v2 capabilities

Default Viewer tickets include the earlier Scene permissions plus:

- `scene.sync`
- `scene.avatar.reconcile`
- `scene.region.metadata`
- `scene.parcel.read`

These capabilities are checked independently at the World Node.

## New Scene v2 messages

| Message | Value | Direction |
| --- | ---: | --- |
| SCENE_SYNC_REQUEST | 132 | Viewer → World |
| SCENE_SYNC | 133 | World → Viewer |
| REGION_METADATA_REQUEST | 134 | Viewer → World |
| REGION_METADATA | 135 | World → Viewer |
| PARCEL_INFO_REQUEST | 136 | Viewer → World |
| PARCEL_INFO | 137 | World → Viewer |
| AVATAR_RECONCILE | 152 | Viewer → World |
| AVATAR_RECONCILE_ACK | 153 | World → Viewer |

The v1 snapshot/events and movement messages remain available for compatibility during development.

## Scene synchronization

`SCENE_SYNC_REQUEST` accepts:

- `since`: last server Scene sequence known to the Viewer
- `max_events`: requested delta batch size, clamped to 1–1024

If `since=0`, or the Viewer is farther than the retained 4096-event history, the server returns:

```text
mode=snapshot
...
```

The payload then contains the full Scene snapshot.

Otherwise the server returns:

```text
mode=delta
...
```

with ordered Scene events after the requested sequence.

This gives Viewers an explicit recovery path after packet loss, reconnects or long stalls without pretending the current TCP transport is already the final production streaming protocol.

## Authoritative Avatar reconciliation

`AVATAR_RECONCILE` accepts:

- monotonically increasing `client_sequence`
- desired transform
- desired linear velocity

The World Node rejects duplicate or stale client sequence numbers.

A successful ACK returns:

- accepted `client_sequence`
- authoritative `server_sequence`
- World simulation tick
- boundary state
- authoritative position
- authoritative rotation
- authoritative velocity

This contract is the first native basis for Viewer-side prediction/reconciliation.

It is intentionally not yet a latency-compensated character-controller protocol.

## Region metadata

`REGION_METADATA_REQUEST` returns:

- Region id
- terrain dimensions/cell size/revision
- water height
- latest Scene sequence
- simulation ticks
- entity/avatar count
- physics body count
- collision/constraint count
- current simulation FPS

The Core Viewer bootstrap additionally exposes the Region's grid coordinate and World Node id.

## Parcel metadata

`PARCEL_INFO_REQUEST` can operate in two modes.

Without coordinates it returns every Parcel known for the current Region.

With `x` and/or `y`, it returns the Parcel covering that point.

Each Parcel record contains:

- id
- name
- owner
- group
- rectangular bounds
- public-entry policy
- public-build policy
- group-build policy
- group-terraform policy

## Existing Scene messages

Scene v2 retains the earlier operations for:

- object create/update/delete
- object permissions
- link/unlink
- floating text
- motion
- Physics v2
- chat
- terrain sampling/editing
- legacy snapshot/events
- legacy avatar movement

## Security

Scene v2 does not trust Viewer-provided identity or Group membership.

Identity, Region, Group ids, capabilities and initial spawn come from the signed Core Scene Ticket.

Object and land mutations continue to combine:

- Scene capability checks
- ownership/group/everyone object permissions
- Parcel policy
- Region moderation

## Current boundaries

The current OGL1 transport is not claimed as the final production-stable transport, but the named Scene v2 application contract is frozen for the 1.0.0-alpha.1 client-development baseline.

Not yet claimed:

- UDP/QUIC transport
- interest-management spatial partitioning
- binary compressed object deltas
- bandwidth prioritization
- final avatar character prediction
- attachment object replication into Scene
- mesh/texture streaming CDN
- final animation protocol
- final camera/controller protocol

Those remain later Viewer/runtime work.
