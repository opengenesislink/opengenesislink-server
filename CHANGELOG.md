# Changelog

## 6.5.0-dev — 2026-09-19

Script World reliability and readback milestone.

- upgraded Script World delivery from destructive polling to a durable file-backed queue
- added per-action leases, explicit World ACK/NACK, retry delay, expiry and bounded attempt count
- pending actions now survive Core restarts
- added Core/World `script_action_result` and `script_action_result_ack` wire messages
- added Script opcodes `object_info`, `region_info`, `terrain_height` and `nearby_avatars`
- World query results are returned over the existing Core/World control connection and persisted into prefixed Script VM variables
- object queries expose transform, physics state and permission masks without granting the VM direct RegionRuntime access
- nearby-avatar results are distance-bounded and capped
- added durable queue restart/ACK/NACK/retry tests and VM-state query-result tests
- added a focused two-process Script World query/ACK smoke test on Linux x86_64 and ARM64
- added a version-aware smoke wrapper so future development version bumps do not require rewriting the large integration smoke
- added `script-world-actions-v2` and `script-world-queries-v1` capability discovery


## 6.0.0-dev — 2026-09-19

Script World-action routing and authenticated writable Hypergrid Inventory milestone.

- added native Script opcodes for object `move`, `rotate`, `scale`, `physics`, `say`, `whisper` and `shout`
- added a bounded `ScriptWorldActionQueue` between Core ScriptHost policy and World Nodes
- added Core/World wire messages for Region-scoped Script action delivery
- Core verifies that a polling World Node owns the requested Region in its current node generation
- World Nodes independently verify target object existence, object type and Script-owner/object-owner binding before mutation
- added RegionRuntime physics enable/disable and distinct whisper/shout scene events
- integrated smoke coverage now moves a real Scene object through the Core-to-World Script path and verifies the resulting Scene snapshot
- writable XInventory now requires a minimum 24-byte shared service key in addition to explicit write enablement
- XInventory write requests must carry the matching `SERVICEKEY`; comparison uses constant-time OpenSSL primitives
- added `hypergrid-xinventory-auth-v1` and `script-world-actions-v1` capability discovery
- added `[scripting].max_pending_world_actions` configuration
- documented current limits: World-action delivery is not yet persistent/ACK-retried, object/avatar read queries remain open, and per-grid signed HG write requests remain future hardening


## 5.5.0-dev — 2026-09-19

Script host-policy and guarded writable Hypergrid Inventory milestone.

- added policy-controlled ScriptHost execution after VM events and timer events
- added `notify` Script opcode for native owner Notifications
- added `message <user-id> <text>` Script opcode for local friend-only direct messages
- Script VM still has no direct filesystem, socket, database or unrestricted service access
- added native Inventory folder/item update, move and delete operations
- upgraded Inventory persistence to v2 with stable legacy folder/item UUID aliases while retaining v1 loading
- added opt-in writable OpenSim XInventory compatibility for folders and items
- added `ADDFOLDER`, `UPDATEFOLDER`, `MOVEFOLDER`, `DELETEFOLDERS`, `PURGEFOLDER`, `ADDITEM`, `UPDATEITEM`, `MOVEITEMS` and `DELETEITEMS`
- XInventory write mode remains disabled by default
- write operations enforce local ownership, folder-parent integrity and Asset export/transfer permissions
- added restart tests for legacy inventory UUID stability
- added ScriptHost tests for Notifications and friend-only messaging
- added API capability discovery for ScriptHost and XInventory v2
- stronger service-to-service authentication for legacy XInventory writes remains production hardening work

## 5.0.0-dev — 2026-09-18

Sandboxed scripting and transactional Region Crossing milestone.

- added a small deterministic event Script VM with explicit opcodes for variables, arithmetic, emitted host actions, state changes, timers and listen channels
- added instruction, variable, state-memory and output-action budgets to stop runaway scripts
- Script VM has no direct filesystem or socket access; effects are returned as explicit host actions
- persisted Script source, source hash and VM state across Core restarts
- added authenticated Script create/list/event Core APIs
- added automatic execution of due timer events in the Core maintenance loop
- extended Region Crossing persistence with Avatar Appearance/attachment context and Script VM state
- added short-lived one-time transactional handoff prepare/complete flow
- signed Crossing IDs into destination Scene Tickets and exposed them during Scene join
- preserved velocity across prepared Region crossings
- rejected Crossing replay, user/destination mismatch and expired crossings
- added Script/Crossing metrics, API discovery and cross-platform tests
- Viewer still coordinates the network connection switch; the transaction now protects and carries runtime state

## 4.5.0-dev — 2026-09-18

Hypergrid messaging, inventory, appearance and return-home milestone.

- added incoming `grid_instant_message` compatibility mapped into the native MessageStore and Notifications
- added outbound HG IM routing through persisted foreign `IMServerURI`
- added a shared portable Hypergrid HTTP/HTTPS callback client with certificate-chain and hostname verification
- persisted foreign Asset, Inventory, Avatar and IM service URLs from AgentCircuitData
- added read-only OpenSim XInventory compatibility for root folder, skeleton, folder content/items, item lookup and Asset permissions
- added OpenSim AvatarService `getavatar` exchange with AvatarHeight, VisualParams, export-safe wearables and attachments
- added authenticated legacy `setavatar` extension for safely importing local-owned wearables, attachments and body data
- extended native Appearance persistence with AvatarHeight and VisualParams while retaining old records
- added `get_home_region` and a persistent returning-home travel state
- added authenticated Core return-home and outbound HG IM APIs
- added integrated and cross-platform tests for HG IM, XInventory, Appearance and return-home
- writable XInventory and the final legacy simulator/viewer data plane remain later hardening work

## 4.0.0-dev — 2026-09-18

Avatar appearance and deeper Hypergrid social/content interoperability milestone.

- added persistent native Avatar Appearance records
- added wearable slots, attachment points and monotonic appearance revisions
- added authenticated Appearance Core APIs with Asset ownership validation
- extended native friendship persistence with directional permissions and interoperability secret metadata
- added Hypergrid Friends compatibility over the native FriendsStore; no separate HG friend database is used
- added HG friend permission lookup, pending/accepted friendship mapping, deletion, incoming offers, offer validation and online-status response
- incoming HG friendship offers now create native OpenGenesisLINK notifications
- added deterministic legacy UUID mapping for exportable Assets
- added Hypergrid Asset GET compatibility for full Asset XML, /data and /metadata
- enforced the native Export permission before any Asset is exposed through Hypergrid
- added cross-platform Avatar/HG social/content tests
- legacy viewer/simulator data-plane handoff, HG IM, remote Inventory and full legacy Appearance transfer remain later work

## 3.5.0-dev — 2026-09-18

Deep Hypergrid session and identity-verification milestone.

- added dedicated Hypergrid compatibility listener, separate from native OGL-FED
- added persistent home-grid travel sessions with OpenSim-style service tokens
- added `verify_agent`, `verify_client`, `agent_is_coming_home` and `logout_agent`
- added deterministic legacy UUID mapping for native OpenGenesisLINK users
- added `/foreignagent` JSON AgentCircuitData parsing
- added HomeURI callback verification before accepting a foreign identity
- added service-token destination binding checks
- added persistent verified foreign-visitor session records and expiry cleanup
- added Core Hypergrid status, travel-issue and session-count APIs
- added XML-RPC request/response callback helpers
- added cross-platform Hypergrid session, circuit and XML-RPC tests
- foreign identity verification is implemented, but the legacy simulator/viewer data plane is not yet claimed complete

## 3.0.0-dev — 2026-09-18

Federation runtime and Hypergrid control-plane milestone.

- added persistent local OGL-FED Grid identity and Ed25519 key storage
- added native Federation runtime coordinator
- added outbound signed Travel Token issuance for authenticated local users
- added inbound trusted Travel Token acceptance with issuer/audience verification and replay rejection
- added persistent foreign visitor sessions with logout and expiry lifecycle
- added destination Region, Estate and Parcel checks before issuing foreign Scene Tickets
- added public Federation info endpoint and admin peer trust/revocation/session APIs
- added Federation metrics and API discovery
- added OpenSimulator Hypergrid XML-RPC codec
- added Hypergrid control-plane handlers for link_region, get_region and get_server_urls
- added deterministic legacy UUID/Region-handle mapping without changing native Region IDs
- added cross-platform Federation/Hypergrid tests
- Hypergrid foreignagent, verify_agent, Friends, IM and Asset compatibility remain later compatibility work

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
