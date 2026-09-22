# OpenGenesisLINK Viewer Contract — 13.0.0-dev

> Release note: OpenGenesisLINK Server 1.0.0-alpha.1 promotes this bootstrap contract to the first client-development release baseline. The canonical Viewer implementation handover is distributed separately from the server repository. Existing `ogl-viewer-bootstrap-v1` fields are retained; breaking changes require a new contract revision.

OpenGenesisLINK 13.0.0-dev adds the first aggregated native Viewer bootstrap contract.

The separate OpenGenesisLINK Viewer project can use this document as the canonical server-side handover for its login/world-entry implementation.

## Core bootstrap

Endpoint:

```text
POST /v1/viewer/bootstrap
Authorization: Bearer <session token>
```

Input:

```json
{
  "region": "region-id",
  "x": 128,
  "y": 128,
  "z": 25
}
```

The same Region, moderation, Estate and Parcel entry policy used by normal Viewer session creation is enforced before a bootstrap response is issued.

## Bootstrap payload

The response contains:

- `viewer_contract = ogl-viewer-bootstrap-v1`
- `scene_contract = scene-v2`
- Scene connection/session data
- user identity
- signed Scene Ticket
- Scene capability list
- server-selected spawn
- Group id CSV
- Avatar Appearance
- wearables
- attachments
- complete current Inventory tree
- owned Asset metadata
- current Region Parcel metadata
- current Region Registry metrics
- Viewer-relevant API/Scene endpoint names

Asset binary payloads are not embedded in bootstrap.

## Appearance

The bootstrap contains the same native Appearance structure exposed by:

```text
GET /v1/avatar/appearance
```

It includes:

- appearance revision
- avatar height
- visual parameter CSV
- wearable slot → Inventory item → Asset references
- attachment point → Inventory item → Asset references

Viewer implementations should treat `revision` as the invalidation/version key for Appearance state.

13.0 exposes attachment metadata to the Viewer, but it does not yet instantiate attachment object graphs as independent replicated Scene entities.

## Inventory

The bootstrap includes:

- Inventory root
- folders
- items
- item → Asset ids

The existing native Core Inventory endpoints remain authoritative for mutation.

## Asset delivery

Bootstrap only returns Asset metadata.

Actual owned Asset data remains available through:

```text
GET /v1/assets
GET /v1/assets/{id}
```

This separation prevents a login bootstrap from transferring every binary Asset.

## Scene entry

Use the returned:

- `scene_endpoint`
- `scene_ticket`
- Region id

Then perform:

1. OGL1 `HELLO`
2. `SCENE_JOIN`
3. `SCENE_SYNC_REQUEST since=0`

The Viewer now has enough server-owned data to build its initial Scene and local Avatar representation.

## Runtime synchronization

After initial snapshot, keep the latest acknowledged Scene sequence.

Request deltas using:

```text
SCENE_SYNC_REQUEST
since=<sequence>
max_events=<1..1024>
```

If history is no longer sufficient the server automatically falls back to a full snapshot.

## Movement

The preferred 13.0 movement request is:

```text
AVATAR_RECONCILE
client_sequence=<monotonic>
x=...
y=...
z=...
rx=...
ry=...
rz=...
vx=...
vy=...
vz=...
```

A Viewer should use the returned authoritative transform/tick/server sequence for prediction correction.

Duplicate/stale client sequence numbers are rejected.

## Teleport and Region crossing

The Viewer continues to use Core for:

- direct Region teleport
- Landmark teleport
- adjacent Region handoff
- handoff reserve/commit/rollback

Scene v2 does not move those trust decisions into the Viewer.

## Parcel / Region UI data

Viewer map/land/property panels can obtain local runtime data from:

- `REGION_METADATA_REQUEST`
- `PARCEL_INFO_REQUEST`

Core remains authoritative for mutation and wider Grid Registry data.

## Viewer implementation order

The separate Viewer project should implement the server contract in this sequence:

1. Core authentication
2. Viewer bootstrap
3. Scene TCP/OGL1 framing
4. HELLO + SCENE_JOIN
5. full Scene sync
6. Avatar reconciliation
7. delta Scene sync
8. Region/Parcel panels
9. Appearance
10. Inventory
11. Asset fetch/render path
12. chat/social UI
13. object/build tools
14. teleport/handoff
15. later Atlas and Voice integration

## Non-claims

13.0 introduced the first coherent **server contract** for a native Viewer; 1.0.0-alpha.1 adopts it as the first released Viewer baseline.

It does not mean the separate OpenGenesisLINK Viewer application itself is implemented or production ready.
