# OpenGenesisLINK Server

OpenGenesisLINK is an independent **C++23** server platform for federated virtual worlds. It is not an OpenSimulator fork. Legacy/OpenSim interoperability is intended to live behind explicit compatibility adapters.

Current development version: **2.0.0-dev**.

`2.0.0-dev` is a development milestone, not a production-stable release. Protocol and persistence formats may still change before a stable release.

## What already runs

- Core and World Node processes with persistent World/Region registries
- generation-based reconnect sessions, node leases and automatic recovery
- multiple adjacent Regions with dedicated Region Runtime tick loops
- 256×256 terrain runtime with persistence
- persistent Scene objects, authenticated Avatar Presence and local chat/events
- native OpenGenesis Physics foundation
- persistent identities and bearer sessions with PBKDF2-HMAC-SHA256 password hashing
- short-lived HMAC-signed Scene Tickets with explicit capabilities, groups and server-selected spawn coordinates
- authenticated avatar movement, Region boundary detection, teleport tickets and adjacent-Region handoff
- live World-to-Core Presence snapshots
- persistent friend requests, friendships and direct messages
- persistent Groups with owner/officer/member roles and server-side powers
- persistent rectangular Parcels with owner/group/public entry, build and terraform policy
- scene-object owner/group/everyone permission masks
- Asset copy/modify/transfer permission masks and next-owner permission reduction
- content-addressed Asset blob storage plus persistent Inventory
- Region/global moderation bans and append-only audit events
- persistent Estates with owner/manager Region policies, capacity and landing points
- persistent user Landmarks integrated with authenticated teleport tickets
- persistent Notifications for Social and Group events
- persistent Group channels and Group notices
- Prometheus-compatible `/metrics` endpoint with low-cardinality Core/Region gauges
- browser-readable Core dashboard and JSON API
- x86_64 and ARM64/aarch64 CI

## Browser interface

Start Core and World, then open:

```text
http://127.0.0.1:18080/
```

The dashboard shows Core, World/Region, simulation, Presence, identity/session, Social, content, governance and world-operations information.

API discovery:

```text
http://127.0.0.1:18080/v1
```

Important development endpoints include:

```text
POST /v1/auth/register
POST /v1/auth/login
GET  /v1/auth/me

POST /v1/viewer/session
POST /v1/viewer/teleport
POST /v1/viewer/handoff
GET  /v1/presence
GET  /v1/regions/<region-id>/neighbors

GET/POST /v1/groups
POST     /v1/groups/members
POST     /v1/groups/role
POST     /v1/groups/remove
GET      /v1/groups/<group-id>/channel
POST     /v1/groups/channel
POST     /v1/parcels
GET      /v1/parcels/owned
POST     /v1/parcels/policy
GET/POST /v1/estates
POST     /v1/estates/managers
POST     /v1/estates/regions
POST     /v1/estates/regions/policy
GET      /v1/regions/<region-id>/estate
GET/POST /v1/landmarks
POST     /v1/landmarks/remove

GET  /v1/social/friends
POST /v1/social/friends/request
POST /v1/social/friends/accept
POST /v1/social/friends/remove
GET/POST /v1/social/messages
GET  /v1/notifications
POST /v1/notifications/read

GET  /metrics

GET  /v1/assets
POST /v1/assets
POST /v1/assets/transfer
GET  /v1/inventory
POST /v1/inventory/folders
POST /v1/inventory/items

GET  /v1/admin/moderation
POST /v1/admin/moderation/ban
POST /v1/admin/moderation/unban
GET  /v1/admin/audit
```

The admin/API and Scene listeners bind to loopback by default. Replace all development secrets before exposing anything beyond a trusted host. TLS termination, key rotation, distributed replay protection, mature admin roles and production abuse controls are not complete yet.

## Authenticated world flow

1. Authenticate with Core and obtain a bearer session.
2. Core checks moderation, Region availability and Parcel entry policy before issuing a Region-specific Scene Ticket.
3. The ticket contains authenticated identity, group memberships, explicit capabilities and server-selected spawn coordinates.
4. World validates signature, expiry, Region binding and the one-time nonce before creating Avatar Presence.
5. Scene operations are checked against ticket capabilities, Parcel policy and object permission masks.
6. `AVATAR_MOVE` updates the avatar and reports a boundary direction when it reaches a Region edge.
7. Core can issue a teleport or adjacent-Region handoff ticket after target policy checks.
8. The destination Region validates the new ticket and creates the authenticated Presence there.

The current adjacent-Region handoff remains client-driven. Seamless cross-World transfer of velocity, attachments and transactional state is not complete yet.

## Build

Requirements: Linux, CMake >= 3.25, Ninja, a C++23 compiler, pthreads and OpenSSL development headers.

Debian/Ubuntu example:

```bash
sudo apt install build-essential cmake ninja-build libssl-dev
./scripts/build.sh
```

Run the full end-to-end test:

```bash
./scripts/smoke-test.sh
```

The smoke test covers two accounts, Social, Groups, Group notices, Notifications, Parcels, Estates, Landmarks, Asset transfer permissions, moderation, audit, authenticated Scene access, movement, teleport/handoff policy, metrics, object/terrain persistence, Core restart and full World restart recovery.

## Documentation

- `docs/BUILD.md`
- `docs/OGL-WIRE-FOUNDATION-v0.md`
- `docs/SCENE-PROTOCOL-v0.md`
- `docs/WORLD-GOVERNANCE-v1.md`
- `docs/WORLD-OPERATIONS-v1.md`
- `docs/PERMISSIONS-v1.md`
- `docs/MODERATION-AUDIT-v1.md`
- `docs/PRESENCE-SOCIAL-v1.md`
- `docs/REGION-HANDOFF-v0.md`
- `docs/CONTENT-SERVICES-v0.md`
- `docs/WEB-API-v1.md`
- `docs/REGION-RUNTIME.md`
- `docs/REGION-PERSISTENCE-v0.md`
- `docs/PHYSICS.md`

## License

Mozilla Public License 2.0. See `LICENSE`.
