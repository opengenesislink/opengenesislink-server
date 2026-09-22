# OpenGenesisLINK Viewer — Server Handover for 1.0.0-alpha.1

## Purpose

This document is the canonical starting specification for the **separate OpenGenesisLINK Viewer project**.

Target server baseline:

```text
OpenGenesisLINK Server 1.0.0-alpha.1
release contract: ogl-release-v1
viewer contract: ogl-viewer-bootstrap-v1
scene contract: scene-v2
scene transport: OGL1 protocol 1
```

The Viewer is not part of the server repository. It must treat the server as authoritative and integrate only through the documented Core HTTP and Scene contracts.

## First connection check

Before login:

```text
GET /v1
GET /v1/release
```

Required release claims:

```text
release_contract = ogl-release-v1
contracts.viewer = ogl-viewer-bootstrap-v1
contracts.scene = scene-v2
```

The Viewer must feature-detect optional capabilities and ignore unknown JSON fields.

## Login and world-entry flow

1. `POST /v1/auth/login`
2. retain the returned bearer session token
3. optionally call `GET /v1/auth/me`
4. call `POST /v1/viewer/bootstrap`
5. read the returned Scene endpoint, Region id, signed Scene Ticket, spawn and capability set
6. connect to the Scene endpoint using OGL1 framed TCP
7. send `HELLO`
8. send `SCENE_JOIN`
9. send `SCENE_SYNC_REQUEST since=0`
10. build the local Scene from the authoritative snapshot
11. continue with delta synchronization and Avatar reconciliation

Do not derive identity, Group membership or Region admission locally. Core places these claims into the signed Scene Ticket.

## Core authentication

Primary endpoints:

```text
POST /v1/auth/register
POST /v1/auth/login
GET  /v1/auth/me
POST /v1/auth/logout
```

Authenticated Core calls use:

```text
Authorization: Bearer <session-token>
```

The Viewer should keep authentication/session handling separate from the Scene connection.

## Viewer bootstrap

Endpoint:

```text
POST /v1/viewer/bootstrap
Authorization: Bearer <session-token>
```

Typical request:

```json
{
  "region": "region-id",
  "x": 128,
  "y": 128,
  "z": 25
}
```

The bootstrap is the preferred login aggregation endpoint.

It supplies the current server-owned state needed for initial Viewer construction, including:

- `viewer_contract = ogl-viewer-bootstrap-v1`
- `scene_contract = scene-v2`
- Scene endpoint/session information
- signed Scene Ticket
- server-selected spawn
- authenticated user identity
- Scene capability list
- Group id context
- Avatar Appearance revision and data
- wearables
- attachments metadata
- Inventory tree
- owned Asset metadata
- current Parcel metadata
- current Region metadata
- relevant Core/Scene endpoint names

Asset binary bodies are fetched separately.

## Scene transport

Scene v2 runs on the existing OGL1 framed transport.

Canonical transport implementation must support:

- frame parsing with bounded lengths
- connection failure/reconnect handling
- HELLO negotiation
- Scene Ticket join
- request/response correlation where required
- monotonic sequence handling
- complete disconnect cleanup

Do not assume that a later UDP/QUIC transport already exists.

## Scene startup messages

Required startup sequence:

```text
HELLO
SCENE_JOIN
SCENE_SYNC_REQUEST
```

HELLO advertises the application contracts and Scene capabilities.

Scene v2 currently identifies:

```text
scene_contract=2
sync=scene-sync-v1
movement=avatar-reconcile-v1
metadata=region-metadata-v1,parcel-read-v1
capabilities=scene-capabilities-v2
```

## Scene synchronization

Use `SCENE_SYNC_REQUEST`.

Important fields:

- `since`: latest Scene sequence already applied
- `max_events`: delta batch request, bounded by server

Behavior:

- `since=0` -> full snapshot
- retained history available -> ordered delta
- retained history unavailable -> server falls back to full snapshot

The Viewer must never assume a local object cache is authoritative after reconnect.

## Avatar movement

Preferred movement path:

```text
AVATAR_RECONCILE
AVATAR_RECONCILE_ACK
```

Send a monotonically increasing `client_sequence`.

The ACK returns authoritative:

- accepted client sequence
- server sequence
- simulation tick
- boundary state
- position
- rotation
- velocity

Initial Viewer prediction should be conservative. The server currently provides a Physics v3 capsule Character foundation, but the final latency-compensated character-controller protocol is a later extension.

## Region and Parcel data

Scene-local read operations:

```text
REGION_METADATA_REQUEST
PARCEL_INFO_REQUEST
```

Core-wide data remains available via Core APIs.

The Viewer should use these for:

- Region information panel
- Parcel/property panel
- local water/terrain metadata
- simulation/debug metrics where shown

## Terrain

The current native Region Runtime uses a 256x256 terrain model.

Viewer responsibilities for the first alpha:

- receive/render terrain state made available by the Scene contract
- render water using Region metadata
- submit only capability-authorized terrain edits
- treat terrain revision as invalidation state

Do not invent client-side land authority.

## Objects

The Scene protocol already exposes operations for:

- create
- update/transform
- delete
- permissions
- link/unlink
- floating text
- motion
- Physics state
- touch interaction

The Viewer should structure object editing as a command layer over server operations, not direct mutation of the local render graph.

Local rendering state may be speculative for responsiveness, but ACK/server deltas remain authoritative.

## Physics and GenesisMesher implications for the Viewer

Server 1.0.0-alpha.1 includes:

- sphere/box/capsule Physics v3 primitives
- scale-aware box state
- primitive raycasts
- capsule character foundation
- jump
- spring constraints
- GenesisMesher procedural box/sphere/cylinder/capsule geometry

The Viewer should keep render mesh and collision representation as separate concepts.

Do not assume arbitrary imported triangle meshes already have production collision support.

## Appearance and attachments

Use the Appearance revision as the invalidation key.

Bootstrap includes:

- Avatar height
- visual parameters
- wearable references
- attachment metadata

The initial Viewer can render attachment inventory/asset references while later Scene revisions add richer attachment object replication.

## Inventory and Assets

Inventory:

```text
GET  /v1/inventory
POST /v1/inventory/folders
POST /v1/inventory/items
```

Assets:

```text
GET  /v1/assets
GET  /v1/assets/{asset-id}
POST /v1/assets
POST /v1/assets/transfer
```

Respect:

- copy permission
- modify permission
- transfer permission
- next-owner permission reduction
- export restrictions for federation/compatibility paths

Cache immutable/content-addressed Asset data where appropriate, but validate metadata/revision/permission context through Core.

## Social, Groups and communication

Viewer UI can be built on existing Core endpoints for:

- Presence
- friends
- direct messages
- blocks/mutes
- Groups
- Group invites
- Group channels/notices
- Notifications

Local chat is a Scene function.

Voice must be abstracted behind the provider-neutral OGL-VOICE/OGL-VOICE-CAP contract. The separate hosted Voice service is not part of this server release.

## Economy and Marketplace

Existing Core contracts support:

- wallet
- ledger
- transfers
- escrow
- Marketplace listings
- Marketplace purchase/fulfillment

The Viewer should display integer minor-unit amounts exactly as returned by the server and never calculate authoritative balances locally.

## Teleport and Region handoff

Core remains the trust boundary.

Use:

```text
POST /v1/viewer/teleport
POST /v1/viewer/handoff
POST /v1/viewer/handoff/reserve
POST /v1/viewer/handoff/complete
POST /v1/viewer/handoff/rollback
```

Adjacent Region crossing has reservation/commit/rollback semantics. The Viewer must be able to recover to the source Region when handoff fails.

## Atlas integration

The Viewer and Atlas are separate projects.

Server-side Atlas contract:

```text
GET /v1/atlas/bootstrap
GET /v1/atlas/regions
GET /v1/atlas/regions/{region-id}
```

The Viewer should expose an internal navigation interface that can accept at minimum:

- Region id
- local X/Y/Z destination

The final operating-system deep-link URI format is intentionally not frozen in this server release and should be versioned by the Viewer project.

## Viewer architecture recommendation

Suggested major modules:

```text
App Shell
├── CoreClient
│   ├── ReleaseDiscovery
│   ├── Auth
│   ├── Inventory/Assets
│   ├── Social/Groups
│   ├── Economy/Marketplace
│   └── Teleport/Handoff
├── SceneClient
│   ├── OGL1 Framing
│   ├── SceneSession
│   ├── Snapshot/Delta Replication
│   ├── AvatarReconciliation
│   └── Scene Commands
├── WorldModel
│   ├── Regions
│   ├── Entities
│   ├── Terrain
│   ├── Parcels
│   └── Avatar State
├── Render
├── Input/Camera
├── UI
├── AssetCache
└── Atlas/Voice adapters
```

Keep networking/contracts independent from the rendering engine so Scene protocol revisions do not require rewriting the renderer.

## Recommended implementation order

1. release discovery
2. Core login/session
3. Viewer bootstrap
4. OGL1 framing
5. HELLO + SCENE_JOIN
6. full Scene snapshot
7. basic Region/terrain/object rendering
8. Avatar rendering
9. Avatar reconciliation
10. Scene deltas/reconnect recovery
11. Asset fetch/cache
12. Appearance/wearables
13. Inventory UI
14. chat/social
15. object editing/build tools
16. Parcel/land UI
17. teleport/handoff
18. Groups/Notifications
19. Economy/Marketplace
20. Atlas bridge
21. Voice provider bridge

## Definition of the first Viewer alpha

The first Viewer alpha should be considered successful when a user can:

- discover a compatible Server
- log in
- enter a Region
- see terrain and replicated objects
- see/control their Avatar
- recover from Scene reconnect
- use local chat
- inspect Inventory/Appearance
- fetch required Assets
- teleport between Regions
- open a Region in Atlas / accept an Atlas destination

Advanced content creation, high-end rendering and final animation/physics parity are later milestones.

## Server documents to keep beside this handover

- `docs/RELEASE-CONTRACT-1.0.md`
- `docs/VIEWER-CONTRACT-13.0.md`
- `docs/SCENE-PROTOCOL-v2.md`
- `docs/CONTENT-SERVICES-v0.md`
- `docs/PERMISSIONS-v1.md`
- `docs/REGION-CROSSING-v3.md`
- `docs/OGL-FED-v2.md`
- `docs/VOICE-PROVIDER-v0.md`
- `docs/PHYSICS.md`
- `docs/GENESIS-MESHER-v1.md`
