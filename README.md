# OpenGenesisLINK Server

OpenGenesisLINK is an independent **C++23** server platform for federated virtual worlds. It is not an OpenSimulator fork. Legacy/OpenSim interoperability is intended to live behind explicit compatibility adapters.

Current development version: **7.5.0-dev**.

`7.5.0-dev` is a development milestone, not a production-stable release. Protocol and persistence formats may still change before a stable release.

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
- native OGL-FED runtime with persistent Grid identity, Ed25519 Travel Tokens, Audience Binding and replay protection
- authenticated outbound Federation travel issuance and signed inbound visitor acceptance
- persistent foreign visitor sessions with logout/expiry lifecycle
- incoming OGL-FED visitors receive destination-bound Scene Tickets after Region/Parcel/Estate policy checks
- persistent Federation trust/revocation store with Core admin API
- transactional Region Crossing v3 records with position, velocity, rotation, angular velocity, Avatar Appearance/attachment context, persistent Script state and bounded physics/linkset context
- two-phase adjacent-Region handoff with destination reservation token, explicit commit, replay rejection and rollback
- crossing IDs remain signed into destination Scene Tickets; destination reservation is separately bound to user, Region and short-lived transaction state
- persistent Object Crossing v1 orchestration across adjacent Regions with Core-mediated export, destination import ACK, source removal and rollback recovery
- single-object crossing preserves ownership, group, permission masks, transform, physical state and linear velocity; destination imports are idempotent
- source objects are removed only after destination import acknowledgement; rollback after import cleans the destination and restores the source from the preserved snapshot, including lost-remove-ACK recovery
- Scene object persistence v4 preserves physical linear velocity while remaining readable from earlier v1-v3 scene records
- sandboxed event Script VM with persistent variables/state, timers, listen channels, explicit host actions and instruction/state/action budgets
- policy-controlled ScriptHost with owner Notifications, local friend-only direct messaging and typed World actions
- durable Core-to-World Script action routing for object move/rotate/scale, physics and local say/whisper/shout with leases, ACK/NACK, retry, expiry and World-side owner validation
- asynchronous Script World queries for object state, Region runtime information, terrain height and nearby Avatars with results persisted back into VM variables
- persistent Script Runtime with source hashing, restart-safe VM state and automatic timer execution through the same ScriptHost policy layer
- reusable runtime Rate Limiter and storage Schema Version guard
- provider-neutral OGL-VOICE / OGL-VOICE-CAP contract for future hosted or self-hosted Voice
- OpenSimulator Hypergrid compatibility gateway with `link_region`, `get_region`, `get_server_urls`, `verify_agent`, `verify_client`, `agent_is_coming_home` and `logout_agent`
- persistent Hypergrid home-travel and verified foreign-visitor sessions
- Hypergrid Friends compatibility mapped directly into the native FriendsStore, including offers, validation, permissions, status and removal
- export-safe Hypergrid Asset GET compatibility for full Asset XML, metadata and raw data with deterministic legacy UUID mapping
- persistent native Avatar Appearance records with wearables, attachments and revision tracking
- authenticated Avatar Appearance Core API with Asset ownership checks
- Hypergrid Instant Messaging with incoming messages persisted in the native MessageStore and Notifications
- outbound HG IM routing using the foreign visitor's advertised IMServerURI
- OpenSim XInventory compatibility with read operations always available and guarded write operations requiring explicit enablement plus a shared service key
- Hypergrid AvatarService exchange for AvatarHeight, VisualParams, export-safe wearables and attachments
- persistent foreign visitor Asset/Inventory/Avatar/IM service URLs
- Hypergrid `get_home_region` and return-home session lifecycle
- shared portable HG HTTP callback client for home verification and IM routing
- `/foreignagent` JSON AgentCircuitData ingestion with HomeURI callback verification and destination/service-token validation; legacy simulator data-plane handoff remains explicitly incomplete
- cross-platform socket layer for Linux/POSIX and Windows Winsock
- Linux x86_64, Linux ARM64/aarch64 and Windows x86_64 CI targets

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
POST /v1/viewer/handoff/reserve
POST /v1/viewer/handoff/complete
POST /v1/viewer/handoff/rollback
GET  /v1/presence

GET/POST /v1/world/object-crossings
POST     /v1/world/object-crossings/rollback

GET  /v1/scripts
POST /v1/scripts
POST /v1/scripts/event
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

GET  /v1/federation/info
POST /v1/federation/travel/issue
POST /v1/federation/travel/accept
GET  /v1/federation/peers
POST /v1/federation/trust
POST /v1/federation/revoke
GET  /v1/federation/sessions
POST /v1/federation/sessions/logout

GET  /v1/hypergrid/info
POST /v1/hypergrid/travel/issue
POST /v1/hypergrid/travel/return
POST /v1/hypergrid/im/send
GET  /v1/hypergrid/sessions

GET  /v1/avatar/appearance
POST /v1/avatar/appearance/wearable
POST /v1/avatar/appearance/wearable/remove
POST /v1/avatar/appearance/attachment
POST /v1/avatar/appearance/attachment/remove
```

The admin/API and Scene listeners bind to loopback by default. Replace all development secrets before exposing anything beyond a trusted host. TLS termination, key rotation, distributed replay protection, mature admin roles and production abuse controls are not complete yet.

### Hypergrid compatibility endpoints

When Hypergrid compatibility is enabled, the dedicated HG listener exposes legacy endpoints such as `/hgfriends`, `/assets/<uuid>`, `/xinventory`, `/avatar` and XML-RPC methods including `grid_instant_message` and `get_home_region`. XInventory reads are available by default. Legacy writes are an explicit opt-in through `hypergrid.inventory_write_enabled = true`; the default remains `false`. 6.0 additionally requires `hypergrid.inventory_write_secret` (minimum 24 bytes) and a matching `SERVICEKEY` on write requests. Folder/item ownership, parent relationships and Asset export/transfer rights are checked server-side. Use HTTPS or a private authenticated transport for the shared key. Outbound legacy callbacks support HTTP and HTTPS; HTTPS uses certificate-chain and hostname verification through OpenSSL.

## Authenticated world flow

1. Authenticate with Core and obtain a bearer session.
2. Core checks moderation, Region availability and Parcel entry policy before issuing a Region-specific Scene Ticket.
3. The ticket contains authenticated identity, group memberships, explicit capabilities and server-selected spawn coordinates.
4. World validates signature, expiry, Region binding and the one-time nonce before creating Avatar Presence.
5. Scene operations are checked against ticket capabilities, Parcel policy and object permission masks.
6. `AVATAR_MOVE` updates the avatar and reports a boundary direction when it reaches a Region edge.
7. Core can issue a teleport or adjacent-Region handoff ticket after target policy checks.
8. The destination Region validates the new ticket and creates the authenticated Presence there.

`7.0.0-dev` upgrades adjacent-Region handoff to Crossing v3. Core prepares a short-lived transaction containing linear and angular motion plus Avatar Appearance/attachment, Script VM, physics and linkset context. After the destination Scene accepts the signed Scene Ticket, the client reserves the Crossing and receives a separate random reservation token. Commit requires that token and the same authenticated user/destination Region. Failed transitions can be rolled back explicitly; expired prepared/reserved crossings are moved to `rolled_back` instead of disappearing immediately. Completed and rolled-back records are retained temporarily for audit/replay diagnostics.

## Federation and Voice foundations

`OGL-FED/1` is the native Federation direction. It uses persistent Grid identity, signed outbound travel, trusted inbound travel verification, replay protection, persistent foreign sessions and destination Scene-Ticket issuance. Hypergrid is implemented as a separate compatibility gateway with its own legacy sessions and verification semantics; OGL-FED keys are never reused as Hypergrid secrets.

Voice is provider-neutral. The server exposes the `OGL-VOICE/1` and `OGL-VOICE-CAP/1` contracts so a Grid can later use the hosted OpenGenesisLINK Voice service, another compatible provider or a self-hosted implementation. Provider credentials stay server-side; the Viewer receives only short-lived destination capabilities.

## Build

Supported build targets are Linux x86_64, Linux ARM64/aarch64 and Windows x86_64 / Windows Server x86_64. CMake >= 3.25, a C++23 compiler and OpenSSL are required. Linux builds use pthreads; Windows uses Winsock through the platform abstraction.

Debian/Ubuntu example:

```bash
sudo apt install build-essential cmake ninja-build libssl-dev
./scripts/build.sh
```

Windows development builds use MSVC, CMake/Ninja and OpenSSL. See `docs/BUILD.md` for the vcpkg example.

Run the full Linux process-level test:

```bash
./scripts/smoke-test.sh
```

The process smoke test covers two accounts, Social, Groups, Group notices, Notifications, Parcels, Estates, Landmarks, Asset transfer permissions, moderation, audit, authenticated Scene access, movement, transactional teleport/handoff, Script VM execution, metrics, object/terrain persistence, Core restart and full World restart recovery. Dedicated Linux process smokes additionally verify Script World query/ACK delivery, Crossing v3 reserve/rollback and end-to-end Object Crossing between adjacent Regions. Cross-platform unit tests additionally cover OGL-FED signing/verification, trust/revocation, replay protection, Hypergrid travel sessions/circuit parsing/XML-RPC callbacks, HG Friends, HG IM, guarded writable XInventory with persistent legacy IDs, export-safe HG Assets, AvatarService exchange, return-home lifecycle, signed one-time Crossing transactions, sandboxed Script VM budgets/state persistence, ScriptHost policy actions, rate limiting, schema versioning and Voice provider validation.

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
- `docs/OGL-FED-v0.md`
- `docs/VOICE-PROVIDER-v0.md`
- `docs/HYPERGRID-COMPAT-v0.md`
- `docs/SCRIPT-RUNTIME-v1.md`
- `docs/SCRIPT-HOST-v1.md`
- `docs/SCRIPT-WORLD-API-v1.md`
- `docs/SCRIPT-WORLD-API-v2.md`
- `docs/HYPERGRID-XINVENTORY-v1.md`
- `docs/REGION-CROSSING-v1.md`
- `docs/REGION-CROSSING-v3.md`
- `docs/OBJECT-CROSSING-v1.md`

## License

Mozilla Public License 2.0. See `LICENSE`.
