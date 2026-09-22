# Changelog

## 1.0.0-alpha.1 — First Client Development Release

Status: release candidate; acceptance requires the final PR head to pass the full cross-platform/database CI matrix.

- transition from internal 17.x development milestones to the first semantic public alpha release line
- add machine-readable `ogl-release-v1` compatibility discovery at `GET /v1/release`
- add dedicated `ogl-atlas-v1` public read API with bootstrap, Region list and Region detail
- freeze `ogl-viewer-bootstrap-v1` and `scene-v2` as the first Viewer-development contract baseline
- define additive/feature-detected alpha compatibility rules
- preserve semantic `VERSION` labels in the compiled server version
- add CPack ZIP/TGZ/DEB packaging foundation
- extend integrated smoke coverage for release discovery and Atlas v1
- add canonical separate-project handovers for Viewer and Atlas
- document deferred simulator features as post-alpha extensions rather than blockers for client development


## 17.0.0-dev — 2026-09-22

Physics v3 & GenesisMesher v1 milestone.

- Started the Physics v3 & GenesisMesher v1 milestone from the accepted 16.0.0-dev main line.
- Selectively ported the reusable native Physics-v3 solver core from the older divergent experimental PR #21 instead of merging that branch over the accepted 16.0 Script changes.
- Added sphere, axis-aligned box and vertical capsule collision-shape state.
- Added primitive/terrain raycasts, capsule character grounded/jump foundation, adaptive bounded substeps and damped spring constraints.
- Added the independent native GenesisMesher v1 subsystem.
- GenesisMesher v1 procedurally triangulates box, sphere/ellipsoid, cylinder and capsule geometry.
- Added mesh validation, degenerate-triangle removal, AABB bounds, deterministic cache keys, UV generation controls and triangle budgets.
- Added collision-representation policy for primitive proxy, convex-hull and triangle-mesh paths without claiming triangle-mesh narrowphase.
- Added bounded GenesisMesher memory caching with hit/miss/eviction metrics.
- Added PhysicsShapeBuilder collision planning between GenesisMesher and the native solver.
- Integrated Physics v3 shape/raycast/spring/character operations with Region Runtime and authenticated Scene Physics v3.
- Added Scene Persistence v7 and Object Crossing transport for collision shape, half extents and capsule height.
- Added dedicated Physics-v3, GenesisMesher and PhysicsShapeBuilder unit-test targets.
- Added `docs/GENESIS-MESHER-v1.md`, `docs/PROJECT-CONCEPT-ROADMAP.md` and the 17.0 wiki handover draft.
- hardened the integrated smoke so its expected API version is derived from `VERSION` instead of a historical hard-coded milestone
- normalized the 17.0 `VERSION` file to a real newline
- final tested PR head: `b7beed4ba4ee67c9c6ad9e850439c4179becda69`
- squash merge: `2291aa5e55369cbf85f15c007183daf12e39b3e9`
- acceptance: Linux x86_64 and ARM64 full build/unit/all-smoke matrix PASS; Windows x86_64/MSVC build+unit PASS; SQLite/PostgreSQL/MariaDB integration PASS


## 16.0.0-dev — 2026-09-21

Script & LSL Event Expansion milestone.

- bumped the development line to `16.0.0-dev`
- expanded deterministic LSL builtin coverage to 62 implemented plus 34 partial functions out of the canonical 523-function catalog
- retained the native OGL catalog at 37/37 implemented
- added automatic Script state lifecycle delivery with `state_exit` followed by `state_entry` on real state transitions
- connected native Region Runtime collision and terrain-contact lifecycle to LSL `collision_start`, `collision`, `collision_end`, `land_collision_start`, `land_collision` and `land_collision_end`
- introduced authenticated Scene object interaction messages and the `scene.object.interact` ticket capability
- Scene interaction now generates server-authoritative `touch_start`, `touch` and `touch_end` events for object-bound scripts
- added first native LSL `changed` bitmask sources: `CHANGED_SCALE = 0x008` for actual scale mutations and `CHANGED_LINK = 0x020` for link/unlink mutations
- link changes are dispatched to the affected linkset members rather than being synthesized only for the initiating object
- added API discovery capabilities `scene-interaction-v1`, `lsl-touch-events-v1` and `lsl-changed-events-v1`
- made the Platform Services smoke version-aware so completed 15.0 regression coverage continues to run unchanged on later development branches
- expanded cross-platform unit coverage for Scene interaction identity validation, touch event sequencing, scale-change masks and link-change masks
- expanded the Core+World ScriptEngine process smoke to exercise real Scene touch and changed event sources instead of only manual Script event injection
- current LSL event matrix: 44 total, 1 implemented, 13 partial, 29 recognized, 1 unsupported; executable including partial is 31.82%
- current LSL function matrix: 523 total, 62 implemented, 34 partial, 402 recognized, 25 unsupported; executable including partial is 18.36%
- verified code checkpoint before milestone documentation: `9100b31a167a7e464145681cbd0d017366aff21f` passed Linux x86_64, Linux ARM64/aarch64, Windows x86_64/MSVC, SQLite, PostgreSQL and MariaDB CI
- final tested PR head: `d8c78aaf58fa03e58692a525366a423206e7e760`
- squash merge: `cf99988f37d0c3abad20454f017afb28ab008588`
- 16.0 does not claim complete detected-touch metadata, complete `CHANGED_*` coverage, attach/detach, permissions events, link messages, sensor events, HTTP/DataServer semantics or full Second Life LSL compatibility

## 15.0.0-dev — 2026-09-20

Platform Services Completion milestone.

- added a provider-neutral native Economy ledger using integer minor units and a configurable currency code
- added persistent wallet accounts, idempotent ledger references, direct user transfers and administrator-authorized mint/burn operations
- added persisted escrow with explicit reserve, commit and release states while preserving total supply across reservations
- added a native Marketplace store with active/reserved/sold/cancelled lifecycle and reservation against double purchase
- Marketplace purchases reserve funds before delivery, copy a transferable Asset to the buyer, create the buyer Inventory item, finalize the listing and only then commit seller credit
- added compensating rollback for logical Marketplace fulfillment/finalization failures, including Inventory removal, buyer Asset compensation removal, escrow refund and listing reactivation where applicable
- added persistent Social block/mute policies; blocks are enforced by friend requests/acceptance, direct messages, Group invitations and direct wallet transfers
- mute suppresses direct-message notifications without discarding the persisted message
- added persistent Group invitations with bounded expiry, target-bound acceptance, revocation and inviter authorization revalidation
- added owner-managed Parcel user allow/deny entries; explicit deny overrides public entry while owner access remains implicit
- added Platform Services metrics and API capability discovery
- extracted 15.0 Platform API routing from the historical monolithic HTTP dispatcher to preserve MSVC portability
- added dedicated Platform Services unit coverage and a Core-level end-to-end smoke covering Social policy, Group invites, Wallet/Ledger, Marketplace fulfillment, rollback and persistence-visible results
- 15.0 does not claim external payment-processor integration, cross-grid economy settlement, a globally distributed ACID transaction across file stores, taxation, auctions, recurring billing or a final stable economy protocol
- final tested PR head: `e82d137d7ed10494b752cbc4d8b21974a52790ac`
- squash merge: `1c39990e016d91e4a434210f0295dbaa8286de64`

## 14.0.0-dev — 2026-09-20

Federation Completion milestone.

- promoted native federation to `OGL-FED/2`
- extended signed Travel Tokens with home-grid routing and scoped remote-service grants while retaining verification compatibility for earlier OGL-FED/1 tokens
- added persistent audience/user/scope-bound Federation Service Grants with bounded lifetime and automatic expiry
- home-grid grant persistence stores only a hash of the service credential
- added native remote federation services for profile, Appearance, Inventory, exportable Assets, Social relations and Presence
- remote Asset delivery requires subject ownership plus native `perm_export`
- added user/operator grant revocation and peer-revocation cascading into grants and active foreign sessions
- pinned trusted peer Ed25519 keys; key rotation requires explicit revoke/re-trust
- foreign sessions persist home-service routing state across restart without exposing service credentials through the admin session API
- added Federation grant metrics and safe administrative grant inspection
- hardened Hypergrid foreign-session persistence against session-ID identity collisions
- retained strict separation between OGL-FED/2 and the explicit OpenSimulator Hypergrid compatibility bridge
- added dedicated OGL-FED/2 remote-services smoke coverage on Linux x86_64 and ARM64
- added canonical `docs/OGL-FED-v2.md` and updated Hypergrid compatibility documentation
- 14.0 does not claim global federation PKI, cross-grid content mutation, economy settlement, a stable 1.0 federation wire format or a completed OpenSimulator legacy data plane
- final tested PR head: `f1e4f5d2d47d9a0901d8677249e3b9be3367eee1`
- squash merge: `de66c4290fe9ab7dcc3fe06068b96dc6fe1f71ad`

## 13.0.0-dev — 2026-09-20

Viewer & Scene Protocol Completion milestone.

- added authenticated `POST /v1/viewer/bootstrap` as the first aggregated native Viewer startup contract
- Viewer bootstrap returns Scene session/ticket, user identity, Avatar Appearance with wearables/attachments, Inventory tree, owned Asset metadata, Region runtime metadata and Parcel metadata
- kept binary Asset payload delivery separate from bootstrap through the existing authenticated Asset API
- introduced Scene Protocol v2 as an application-level contract on the existing OGL1 framed transport
- added `SCENE_SYNC_REQUEST/SCENE_SYNC` with ordered delta batches and automatic full-snapshot recovery when the retained event history is insufficient
- added `REGION_METADATA_REQUEST/REGION_METADATA` for terrain, water, sequence and runtime/physics metrics
- added `PARCEL_INFO_REQUEST/PARCEL_INFO` for Region-wide or point-based Parcel metadata reads
- added sequenced `AVATAR_RECONCILE/AVATAR_RECONCILE_ACK` with stale/replayed client sequence rejection
- Avatar reconciliation returns authoritative Scene sequence, simulation tick, transform, velocity and boundary state for Viewer prediction correction
- Scene ticket defaults now include `scene.sync`, `scene.avatar.reconcile`, `scene.region.metadata` and `scene.parcel.read`
- API discovery advertises `viewer-bootstrap-v1`, `scene-protocol-v2`, `scene-sync-v1`, `avatar-reconcile-v1`, `region-metadata-v1` and `parcel-read-v1`
- added `docs/SCENE-PROTOCOL-v2.md` and `docs/VIEWER-CONTRACT-13.0.md` as the canonical server handover for the separate Viewer project
- added a dedicated Core → Viewer bootstrap → Scene v2 end-to-end smoke test to Linux x86_64 and ARM64 CI
- 13.0 defines the native server contract only; it does not claim that the separate OpenGenesisLINK Viewer application is implemented or that the stable 1.0 wire protocol is frozen
- final tested PR head: `b677ba64cd58c45cf747ba4ee11e13a253dc90a9`
- squash merge: `1b4c9454c4b15693c7a07772f7ac133f80888615`

## 12.0.0-dev — 2026-09-20

World Runtime & Physics v2 milestone.

- promoted the native OpenGenesis physics layer to Physics v2
- added deterministic body-vs-body sphere collision resolution with penetration correction, restitution and friction impulses
- added terrain contact reporting with start/stay/end lifecycle
- added force, linear impulse, angular impulse and torque application
- added mass, restitution, friction, linear/angular damping, gravity scale and buoyancy body state
- added bounded distance constraints with automatic cleanup when referenced bodies are removed
- added Region Runtime collision metrics and Scene events: `collision_start`, `collision`, `collision_end`, `land_collision_start`, `land_collision`, `land_collision_end`
- upgraded linked-object motion so children follow root translation and rotation as rigid offsets
- introduced authenticated Scene Physics v2 mutation messages for material, force, impulse, angular impulse, torque, buoyancy and distance constraints
- added Physics v2 state to object inspection and Scene snapshots
- added six native OGL Physics v2 commands: `world.force`, `world.impulse`, `world.angular_impulse`, `world.torque`, `world.buoyancy`, `world.material`
- OGL 12.0 catalog is 37/37 implemented while retaining the completed 31/31 OGL-v2 language core
- added partial executable LSL mappings for `llApplyImpulse`, `llApplyRotationalImpulse`, `llSetForce`, `llSetTorque` and `llSetBuoyancy`
- LSL function status is 56 implemented, 29 partial, 413 recognized and 25 unsupported out of 523; executable including partial is 16.25%
- upgraded Scene Persistence to v6 and preserved Physics v2 material/motion state across persistence and Object Crossing
- retained backward loading of earlier Scene persistence layouts
- expanded unit, authenticated Scene, ScriptEngine and crossing coverage for Physics v2
- 12.0 does not claim final mesh/convex collision, continuous collision detection, final avatar character controller, full vehicle semantics or complete LSL compatibility
- final tested PR head: `5d4db8dd7a8340c286d716e793380689f5d0b9e5`
- squash merge: `414a65e8b01cf9aded64e13f183eea24df69e3c7`

## 11.0.0-dev — 2026-09-20

Script Runtime Completion milestone.

- upgraded the shared Script VM with bounded unconditional and conditional jumps while preserving the existing instruction, state and action budgets
- added compile-time jump target validation so malformed control-flow programs cannot escape handler bounds
- introduced OGL v2 with `if / else / endif`, `while / endwhile`, bounded `for / endfor`, reusable parameterless `function / call / endfunction` procedures and typed declarations
- added typed OGL declarations for integer, float, bool, string, key, vector, rotation and list values
- completed the defined OGL command/feature matrix from 27/31 to 31/31 implemented (100.00%)
- expanded deterministic sandboxed LSL builtin coverage from 27 to 56 strictly implemented catalog entries
- added LSL list conversion, extraction, slicing, search, insertion and replacement helpers
- added URL escaping/unescaping, generated key and SHA-1 helpers
- added quaternion/euler/axis-angle conversion, rotation/vector direction and rotation-angle helpers
- LSL catalog remains explicit: 523 functions total, 56 implemented, 24 partial, 418 recognized and 25 unsupported
- LSL executable coverage including partial entries is 15.30%; 11.0 does not claim full Second Life LSL semantic compatibility
- upgraded the ScriptEngine process smoke to execute OGL v2 through Core + World
- added unit coverage for OGL v2 control flow, functions, typed values and the expanded LSL deterministic builtin set
- API discovery now advertises `script-engine-v3` and `script-language-ogl-v2`
- exact pre-documentation CI head `6d74e79bde8cc64f78860675cd078a94a68f4fce` passed Linux x86_64, Linux ARM64/aarch64, Windows x86_64 and Database Integration CI
- final tested PR head: `d26cbccbdd3f60e259668961d9da2b3427baee80`
- squash merge: `8c94dd35aa25713e46f1ae2eb5dd9a37f0860d48`

## 10.0.0-dev — 2026-09-20

Production Server Foundation milestone.

- added a real relational database driver layer for SQLite, PostgreSQL and MariaDB/MariaDB Connector-C
- added prepared parameters for all three backends, bounded connection pools, transactions, ping/reconnect handling and database health counters
- added cross-database schema migrations with schema-version tracking and idempotent migration execution
- moved Identity, Auth Sessions, World Registry, Region Registry, Audit, Moderation and Admin Roles to SQL-authoritative stores when a database backend is configured
- retained the existing file-store paths as explicit legacy/development compatibility mode
- added `[database]` configuration for backend, SQLite path, host, port, database, user, password/environment password, pool size, connection timeout and TLS policy
- added production-mode validation that rejects development secrets and supports environment-backed Scene Ticket, Admin API, World Node and database credentials
- added storage visibility to `/health`, `/v1/storage/status`, `/v1/status` and Prometheus metrics
- added the `opengenesis-storage` utility for storage status/schema inspection
- added live database integration CI covering SQLite, PostgreSQL 17 and MariaDB 11.8
- Linux x86_64 and Linux ARM64/aarch64 build and run the production database client layer under warnings-as-errors
- Windows x86_64/MSVC builds the same production foundation through vcpkg and warnings-as-errors
- fixed MariaDB numeric result decoding after live integration exposed truncated one-byte result buffers
- made environment-secret loading portable and MSVC-safe
- final tested PR head: `c8d72075feee07fe091ceecc3f70461f125ced90`
- squash merge: `d895d8890346a12595590139254ac2e0f3a96298`

## 9.0.0-dev — 2026-09-19

Multi-language ScriptEngine, native OGL language and LSL compatibility milestone.

- introduced a shared `OGL ScriptEngine` architecture so legacy IR, LSL compatibility source and native OGL source execute through one sandbox/security/runtime model
- Script records now persist their language as `legacy`, `lsl` or `ogl`; older Script Runtime v1/v2 records remain readable as legacy scripts
- Script handlers are now bound to logical states while legacy handlers remain wildcard-state compatible
- event handlers can declare parameters; bounded newline-delimited event payloads are mapped into VM variables
- added native OGL language v1 with `state`, `on`, `let`, `inc`, `goto`, timer/listen/social actions, World mutations and World queries
- added an LSL compatibility frontend for `default` and named states, event declarations, state transitions, typed event parameter names, function calls and initialized local declarations
- added executable LSL mappings for public say/whisper/shout, owner notification, direct messages, timers, listen registration, position/scale/velocity/angular velocity, floating text, physics status and reset-to-default behavior
- added deterministic LSL built-ins for core math, strings, Base64, vectors, SHA-256 and time/date operations
- added a machine-readable catalog containing 523 canonical LSL function identifiers and the 44 LSL event-category entries used by the compatibility status API
- added `GET /v1/scripts/capabilities` with per-command status and implementation/executable percentages
- authenticated Script creation now accepts `language: "legacy"|"lsl"|"ogl"`
- Script API records expose the persisted language; authenticated event injection accepts an optional bounded payload
- added a dedicated Core+World OGL/LSL ScriptEngine process smoke to both Linux CI architectures
- status metrics deliberately distinguish catalog coverage from executable semantics; 9.0 does not claim full Second Life LSL semantic compatibility


## 8.0.0-dev — 2026-09-19

Object Runtime v1, Linkset Crossing v2 and Script World API v3 milestone.

- added native root/child linkset topology to RegionRuntime with bounded 64-member transfer graphs
- added link/unlink operations, link numbers, floating object text and root-driven linked translation
- PhysicsWorld bodies now carry rotation and angular velocity and integrate angular motion during the native simulation tick
- added configurable Region water height and exposed it through Scene/runtime queries
- upgraded Scene protocol with entity link, text and motion messages under explicit capabilities
- upgraded Scene persistence to v5 with parent entity IDs, link numbers, floating text and angular velocity while retaining v1-v4 read compatibility
- upgraded Object Crossing to v2 linkset snapshots with deterministic per-member destination IDs and restart-safe source-to-destination entity maps
- destination import remains idempotent; source linksets are removed only after destination import acknowledgement
- Script object bindings are migrated atomically after successful source removal, so attached Script execution follows the committed destination entity IDs
- exported transactions now enter destination cleanup on rollback/expiry instead of blindly declaring rollback, covering the lost destination-import-ACK race
- linkset snapshots are bounded to 256 KiB and entity maps to 16 KiB
- Script World API v3 adds linear velocity, angular velocity and floating-text mutations plus water-level and world-time queries
- object queries now expose velocity, angular velocity, parent entity, link number, linkset size and floating text
- upgraded the Linux Object Crossing process smoke to create and migrate a real root/child linkset
- added Windows vcpkg binary caching to reduce repeated OpenSSL dependency installation work


## 7.5.0-dev — 2026-09-19

Object Crossing v1 orchestration milestone.

- added a persistent `ObjectCrossingStore` for adjacent-Region Scene-object migration
- added the transactional forward path `prepare → export → import → remove → completed`
- added rollback recovery `imported → cleanup_pending → restore_pending → rolled_back`
- source objects are not removed until the destination World Node has imported and acknowledged the object
- lost source-remove acknowledgements are recoverable: rollback removes the destination copy and reconstructs the source from the preserved snapshot
- destination imports use deterministic transfer entity IDs and are idempotent across command retries
- transfer snapshots preserve object name, owner, group, owner/group/everyone permissions, transform, physical flag and linear velocity
- added Core/World message types for object-crossing poll, command, result and result acknowledgement
- Core accepts crossing commands/results only from the World Node owning the Region in its current node generation
- added authenticated `GET|POST /v1/world/object-crossings` and `POST /v1/world/object-crossings/rollback`
- crossing preparation validates Region adjacency/online state, moderation, Estate policy and destination Parcel build policy; source ownership is revalidated by the source World Node
- added bounded forward-phase retry counters, safety reconciliation for cleanup/source restoration, expiry handling and 24-hour terminal retention
- upgraded Scene-object persistence to v4 so physical linear velocity survives World restart while v1/v2/v3 records remain readable
- added Object Crossing state-machine/runtime unit coverage, including the lost-remove-ACK rollback race
- added an end-to-end Core+World Scene-object crossing smoke test on Linux x86_64 and ARM64
- Object Crossing v1 intentionally covers single Scene objects; linksets, attachment graphs, vehicle state, angular velocity and object-attached Script execution ownership remain later work


## 7.0.0-dev — 2026-09-19

Transactional Region Crossing v3 milestone.

- upgraded adjacent-Region handoff from one-step completion to a two-phase `prepare → reserve → commit` transaction
- destination reservation generates a separate 192-bit random reservation token and is idempotent for safe client retries
- commit now requires the same authenticated user, destination Region and reservation token
- incorrect reservation tokens, commit-before-reserve and commit replay are rejected
- added explicit authenticated `POST /v1/viewer/handoff/rollback` for prepared/reserved transactions
- expired prepared/reserved Crossings transition to `rolled_back` with a persisted reason instead of being immediately deleted
- terminal Crossing records are retained for 24 hours for replay/audit diagnostics before cleanup
- Crossing v3 persistence now carries position, linear velocity, rotation, angular velocity, Avatar Appearance/attachment context, Script VM context, physics context and linkset context
- CrossingStore v3 remains backward-readable for v1/v2 persisted records
- handoff preparation accepts bounded rotation/angular-velocity context and records server-generated physics context
- API discovery now advertises `region-handoff-v2`, `crossing-v3` and `crossing-reservation-v1`
- integrated Linux smoke is upgraded at runtime to exercise reserve/commit and motion-context preservation
- Script/Crossing unit coverage includes idempotent reservation, wrong-token rejection, replay rejection and idempotent rollback


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
