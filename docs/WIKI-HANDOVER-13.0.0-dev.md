# Wiki Handover — OpenGenesisLINK Server 13.0.0-dev

## Milestone identity

OpenGenesisLINK Server 13.0.0-dev is the **Viewer & Scene Protocol Completion** milestone.

- repository: `opengenesislink/opengenesislink-server`
- pull request: #18
- final tested PR head: `b677ba64cd58c45cf747ba4ee11e13a253dc90a9`
- squash merge: `1b4c9454c4b15693c7a07772f7ac133f80888615`
- version: `13.0.0-dev`
- implementation language: C++23
- license: MPL 2.0
- first-class targets: Linux x86_64, Linux ARM64/aarch64 and Windows x86_64/MSVC

This milestone defines the first coherent native server-side contract for the separate OpenGenesisLINK Viewer project.

It does **not** mean the Viewer application itself is implemented.

## Architecture

The Viewer-facing path is now:

```text
Viewer
  │
  ├── HTTPS/Core API
  │     ├── auth
  │     ├── viewer bootstrap
  │     ├── appearance
  │     ├── inventory
  │     ├── assets
  │     ├── teleport/handoff
  │     └── social/group services
  │
  └── OGL1 Scene connection
        ├── HELLO
        ├── SCENE_JOIN
        ├── Scene v2 sync
        ├── avatar reconciliation
        ├── region/parcel metadata
        ├── object/build operations
        ├── chat
        ├── terrain
        └── Physics v2
```

Core remains the trust/identity/content authority. World Nodes remain the live Scene simulation authority.

## Viewer bootstrap v1

Endpoint:

`POST /v1/viewer/bootstrap`

Authentication:

`Authorization: Bearer <Core session token>`

Input accepts:

- Region id
- optional X/Y/Z spawn request

Before issuing bootstrap, Core enforces the same policy used for Viewer sessions:

- valid authenticated session
- online Region
- online World Node
- moderation bans
- Estate entry policy
- Region capacity
- Parcel entry policy

## Bootstrap response

The response declares:

- `viewer_contract = ogl-viewer-bootstrap-v1`
- `scene_contract = scene-v2`

It aggregates:

- authenticated user id/display name
- target Region id/name
- Scene endpoint
- signed short-lived Scene Ticket
- Scene capabilities
- Group ids
- server-approved spawn
- Scene Ticket expiry
- Avatar Appearance
- Appearance revision
- visual parameters
- wearables
- attachment metadata
- Inventory root/folders/items
- owned Asset metadata
- Region Parcel metadata
- Region grid coordinate
- World Node id
- latest Core-side Region metrics
- Viewer-relevant endpoint map

Binary Asset payloads are intentionally not embedded.

## Asset and Inventory path

The Viewer bootstrap gives the Viewer the identifiers needed to construct UI state.

Binary Asset content continues to use:

- `GET /v1/assets`
- `GET /v1/assets/{id}`

Inventory continues to use the native Core Inventory API.

This avoids transferring every binary Asset during login.

## Appearance and attachments

The bootstrap embeds the current native Avatar Appearance record.

It contains:

- revision
- avatar height
- visual parameter CSV
- wearable references
- attachment point/item/Asset references

13.0 provides the server contract required for Viewer-side attachment discovery.

It does not yet claim that attachment object graphs are fully instantiated and streamed as independent Scene entities.

## Scene Protocol v2

Scene v2 remains on the existing OGL1 frame transport.

Transport frame protocol version remains 1.

The Scene application contract advertises:

- `scene_contract=2`
- `sync=scene-sync-v1`
- `movement=avatar-reconcile-v1`
- `metadata=region-metadata-v1,parcel-read-v1`
- `capabilities=scene-capabilities-v2`

Canonical protocol document:

`docs/SCENE-PROTOCOL-v2.md`

## Scene synchronization

New messages:

- `SCENE_SYNC_REQUEST = 132`
- `SCENE_SYNC = 133`

A Viewer sends:

- last known Scene sequence
- requested maximum event count

The requested event count is clamped to 1–1024.

The server returns a delta when the requested sequence can be satisfied from retained event history.

The server returns a full snapshot when:

- `since=0`
- or the Viewer is beyond the retained 4096-event history window

This gives the Viewer an explicit recovery mechanism after reconnects or long stalls.

## Region metadata

New messages:

- `REGION_METADATA_REQUEST = 134`
- `REGION_METADATA = 135`

The response exposes:

- Region id
- terrain dimensions
- terrain cell size
- terrain revision
- water height
- latest Scene sequence
- simulation ticks
- entity count
- Avatar count
- Physics body count
- active collision contact count
- Physics constraint count
- simulation FPS

## Parcel metadata

New messages:

- `PARCEL_INFO_REQUEST = 136`
- `PARCEL_INFO = 137`

Without coordinates the server returns all Parcels for the current Region.

With X/Y coordinates the server returns the Parcel at that position.

Parcel records expose:

- id
- name
- owner
- Group
- bounds
- public entry
- public build
- Group build
- Group terraform

This is a read contract. Core remains authoritative for Parcel mutation.

## Avatar reconciliation

New messages:

- `AVATAR_RECONCILE = 152`
- `AVATAR_RECONCILE_ACK = 153`

The Viewer supplies a strictly increasing `client_sequence`.

The server rejects zero, duplicate or stale sequence values.

A successful ACK contains:

- accepted client sequence
- authoritative server Scene sequence
- simulation tick
- boundary state
- authoritative X/Y/Z
- authoritative rotation
- authoritative linear velocity

This is the first native basis for Viewer-side movement prediction and correction.

The older `AVATAR_MOVE` contract remains available during development compatibility.

## Scene v2 ticket capabilities

Default native Viewer Scene Tickets now include:

- `scene.sync`
- `scene.avatar.reconcile`
- `scene.region.metadata`
- `scene.parcel.read`

They retain the existing join/read/move/chat/object/terrain capabilities.

The World Node checks capabilities independently for each Scene v2 operation.

## Security boundary

13.0 does not move authority into the Viewer.

The Viewer cannot self-assert:

- identity
- Groups
- Scene permissions
- spawn trust
- land access
- object ownership

Those remain server-authoritative.

Scene Tickets are signed, Region-bound and short-lived.

## Verification

The dedicated 13.0 smoke performs:

1. Core startup
2. World startup/registration
3. account registration
4. Viewer bootstrap
5. bootstrap Appearance/Inventory/Asset/Region validation
6. OGL1 Scene connection
7. HELLO v2 contract validation
8. SCENE_JOIN
9. Region metadata read
10. Parcel metadata read
11. initial full Scene sync
12. sequenced Avatar reconciliation
13. stale client sequence rejection
14. delta Scene sync after movement
15. graceful disconnect

Final acceptance requires:

- Linux x86_64 CI
- Linux ARM64/aarch64 CI
- Windows x86_64/MSVC CI
- SQLite integration
- PostgreSQL integration
- MariaDB integration
- all existing unit/smoke/crossing tests
- dedicated Viewer/Scene v2 smoke

## Acceptance result

All required gates passed on the final PR head `b677ba64cd58c45cf747ba4ee11e13a253dc90a9`:

- Linux x86_64: PASS
- Linux ARM64/aarch64: PASS
- Windows x86_64/MSVC: PASS
- unit tests: PASS
- integrated world/social/handoff smoke: PASS
- Script World query/ACK smoke: PASS
- OGL/LSL ScriptEngine smoke: PASS
- Crossing v3 smoke: PASS
- Object Crossing smoke: PASS
- Viewer bootstrap / Scene v2 smoke: PASS on x86_64 and ARM64
- SQLite integration: PASS
- PostgreSQL integration: PASS
- MariaDB integration: PASS

Canonical 13.0 squash merge:

`1b4c9454c4b15693c7a07772f7ac133f80888615`

## Server state after 13.0

13.0 does not replace the 12.0 Physics v2 work.

The server now combines:

- production database/storage foundation
- native World/Region Runtime
- Physics v2
- Object Runtime v2
- ScriptEngine/OGL/LSL
- transactional avatar/object crossing
- native Federation/Hypergrid compatibility foundations
- Social/Land/Asset/Inventory services
- Viewer bootstrap v1
- Scene Protocol v2

## Explicit non-claims

13.0 does not claim:

- completed OpenGenesisLINK Viewer application
- final stable 1.0 wire format
- UDP/QUIC transport
- final interest-management/spatial streaming
- compressed binary Scene deltas
- production bandwidth prioritization
- final character prediction/controller
- full attachment Scene replication
- final animation/camera protocol
- Atlas integration
- Voice implementation

## Next roadmap block

After 13.0, the logical next server milestone is **14.0.0-dev — Federation Completion**.

That block should finish native GenesisLink/OGL-FED travel and trust semantics, remote assets/social/inventory contracts and harden the explicit OpenSimulator Hypergrid bridge without coupling the native Viewer protocol back to OpenSimulator.
