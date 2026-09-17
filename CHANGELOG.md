# Changelog

## 2.5.0-dev — 2026-09-18

Federation, runtime-foundation and cross-platform milestone.

- added cross-platform socket abstraction for Linux/POSIX and Windows Winsock
- added Windows Server x86_64 MSVC build/test CI with warnings-as-errors
- added Ed25519 Grid key generation and detached signature verification
- added OGL-FED/1 signed Travel Tokens with issuer/audience binding, expiry and nonce
- added one-time Travel replay cache
- added persistent Federation peer trust and revocation store
- added persistent transactional Region Crossing records with one-time completion, position and velocity
- added persistent Script Event Runtime foundation with states, timers and chat events
- added reusable keyed runtime Rate Limiter
- added persistent storage Schema Version guard and ordered upgrade support
- added provider-neutral OGL-VOICE/1 and OGL-VOICE-CAP/1 server contracts
- added cross-platform federation/runtime unit test suite
- documented Windows build targets, OGL-FED and Voice-provider separation
- OpenSimulator Hypergrid remains a separate legacy compatibility adapter and is not claimed complete in this milestone

## 2.0.0-dev — 2026-09-16

World-operations, Estate, Landmark, notification and observability milestone.

- added persistent Estates with owner and manager authorization
- added persistent Region Estate policies for access, capacity, maturity and landing coordinates
- enforced Estate public access and maximum Region capacity during session, teleport and handoff ticket issuance
- added persistent user Landmarks and Landmark-driven authenticated teleport
- added persistent Notifications with unread/read state
- wired friend requests, friendship acceptance, direct messages, Group membership and Group notices into Notifications
- added persistent Group channels and officer/owner Group notices
- extended Group power masks with notice publication permission and migration from existing role records
- added Prometheus-compatible `/metrics` endpoint with Core and per-Region gauges
- expanded browser dashboard and API discovery for world-operations services
- expanded unit tests for Estates, Landmarks, Notifications and Group channels
- expanded integrated smoke coverage across Estates, Landmarks, Notifications, Group notices and metrics
- hardened process-stop handling in recovery tests to avoid unbounded waits

## 1.5.0-dev — 2026-09-16

World-governance, permissions and controlled-travel milestone.

- added persistent Groups with owner/officer/member roles and power masks
- added persistent rectangular Parcels with owner/group/public entry, build and terraform policy
- embedded authenticated group memberships and server-selected spawn coordinates in signed Scene Tickets
- enforced Parcel policy at Scene join, object creation/movement and terrain mutation
- added owner/group/everyone scene-object permission masks and runtime permission updates
- added Asset copy/modify/transfer masks, next-owner permission reduction and ownership transfer without blob duplication
- added authenticated Core teleport tickets with moderation and Parcel-entry checks
- strengthened adjacent-Region handoff with target spawn selection and governance checks
- added Region/global moderation bans checked by Core and World Node
- added append-only audit events for sensitive governance operations
- expanded browser status with Group, Parcel, moderation and audit counters
- expanded unit tests for Groups, Parcels, Asset transfer permissions, moderation, audit and object permissions
- expanded end-to-end smoke test across Social, Groups, Parcels, transfer, ban/unban, teleport, handoff and restart persistence
- kept development admin keys and shared single-host policy files explicitly non-production

## 1.0.0-dev — 2026-09-16

Integrated Presence, Social, Movement and Region-handoff development milestone.

- added live World-to-Core Presence snapshots with authenticated user, Region, entity and position information
- added persistent friend requests, acceptance/removal and friendship queries
- added persistent direct messaging with unread/read state
- added Scene Ticket capability claims and server-side capability enforcement
- added native `AVATAR_MOVE` protocol messages and avatar velocity/transform updates
- added Region boundary detection for north/south/east/west exits
- added Core Region-neighbor discovery from grid coordinates
- added authenticated adjacent-Region handoff ticket endpoint
- added handoff origin claims to Scene Tickets
- expanded Core status and browser dashboard with Presence and Social metrics
- default World configuration now demonstrates two adjacent Regions
- expanded unit tests for Presence, Social persistence, capabilities, movement and adjacency
- expanded end-to-end smoke test across two accounts, messaging, Presence, movement, handoff, Core restart and World restart
- verified Debug, Release and warnings-as-errors builds

## 0.5.0-dev — 2026-09-15

Authenticated viewer and first content-services milestone.

- added short-lived HMAC-SHA256 signed Scene Tickets issued by Core
- added region binding, expiry validation, random ticket nonce and runtime replay rejection
- Scene join now requires a valid Core-issued ticket
- Avatar user id and display name now originate from authenticated Core identity
- added scene-object ownership and owner persistence
- object update/delete now require ownership in the current Scene session
- added persistent Asset metadata and content-addressed blob storage
- added authenticated Asset upload/list/read Web API
- added persistent Inventory roots, folders and asset-backed items
- added authenticated Inventory Web API
- expanded browser dashboard with content-service information
- added `/v1/viewer/session` and content capability discovery
- expanded end-to-end tests across Core and World restarts
- added unit tests for ticket signing, region binding, tamper detection, Asset ownership and Inventory persistence


## 0.4.0-dev — 2026-09-15

Identity, persistence and browser/API milestone.

- added persistent Core identity store
- added PBKDF2-HMAC-SHA256 password hashing with random salts using OpenSSL
- added persistent hashed bearer-session store and cryptographically random session tokens
- added register, login, current-user and logout HTTP endpoints
- added browser-readable Core dashboard at `/`
- added API discovery at `/v1`
- added dedicated `/v1/worlds`, `/v1/regions`, `/v1/identity/stats` and `/v1/identity/users` endpoints
- expanded `/v1/status` with uptime, identity and session information
- added persistent per-region scene-object storage
- added binary terrain persistence and restart restore
- added Scene protocol terrain mutation (`TERRAIN_SET`)
- World Node persistence continues independently of Core connectivity
- expanded unit tests for Identity, Sessions and Region persistence
- expanded smoke test across Core restart and full World Node restart
- kept public repository limited to source, configuration and technical documentation

## 0.3.0-dev — 2026-09-15

Scene Runtime milestone.

- added 256×256 heightfield terrain runtime
- added scene entities and avatar presences
- added object create/update/delete, local chat, snapshots and event stream
- added first development Scene endpoint
- coupled native Physics ground sampling to terrain
- Region Runtime continues while Core is unavailable
- fixed OGL header byte layout and locked it with an interoperability test

## 0.2.0-dev — 2026-09-15

First larger runtime milestone.

- generation-based World Node reconnect sessions
- node leases and stale-node expiry foundation
- live Region Runtime and metrics
- native OpenGenesis Physics kernel
- HTTP health/status endpoint

## 0.1.1-dev — 2026-09-15

- persistent World Node and Region registries
- region lifecycle and ownership checks
- grid-coordinate collision protection

## 0.1.0-dev — 2026-09-15

- initial C++23 Core/World foundation
- initial OGL wire framing and TCP registration
