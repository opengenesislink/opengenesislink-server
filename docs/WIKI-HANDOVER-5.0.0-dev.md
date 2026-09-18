# OpenGenesisLINK Wiki Handover — 4.5.0-dev → 5.0.0-dev

> Purpose: source material for the OpenGenesisLINK wiki.
>
> This file is intentionally **not** structured as one final wiki page. The wiki implementation may split, merge and place the sections below into whichever existing or new subpages fit best.

## Metadata

- Project: OpenGenesisLINK Server
- Current version: `5.0.0-dev`
- Language: C++23
- License: MPL 2.0
- Current milestone commit: `517957b067bf1ce35eb28a4887cd4f74199573d2`
- Covered development range: `4.5.0-dev` through `5.0.0-dev`
- Native federation protocol: `OGL-FED/1`
- Hypergrid remains a separate legacy compatibility layer
- Supported CI targets: Linux x86_64, Linux ARM64/aarch64 and Windows x86_64/MSVC

---

## Executive summary

The 4.5 and 5.0 milestones deepen two major parts of OpenGenesisLINK:

1. OpenSimulator Hypergrid interoperability now covers messaging, read-only inventory access, Avatar Appearance exchange and return-home lifecycle in addition to the previously implemented Gatekeeper, Friends and Asset compatibility.
2. The native OpenGenesisLINK runtime now has a sandboxed persistent Script VM and transactional Region Crossing that carries velocity, Avatar/attachment context and Script state across adjacent-Region handoffs.

These features remain separate by design: native OpenGenesisLINK federation/runtime functionality is not implemented by reusing Hypergrid credentials or OpenSimulator internals.

---

# 4.5.0-dev — Hypergrid Messaging, Inventory, Appearance and Return Home

## Hypergrid Instant Messaging

OpenGenesisLINK accepts legacy `grid_instant_message` calls through the Hypergrid compatibility gateway.

Incoming Hypergrid IMs are translated into the native OpenGenesisLINK messaging system:

- message persisted in the native MessageStore
- local recipient resolution
- Notification creation for the local user
- foreign sender identity preserved
- legacy request receives an OpenSim-compatible success/failure response

Outbound HG IM uses the foreign visitor service routing learned from AgentCircuitData.

The visitor record persists:

- AssetServerURI
- InventoryServerURI
- AvatarServerURI
- IMServerURI

Outbound IM is routed to the visitor's advertised `IMServerURI`, rather than hard-coding one remote endpoint.

### Security

- remote service URLs remain part of the Hypergrid compatibility session
- HTTP and HTTPS callbacks share the portable HG HTTP client
- HTTPS verifies certificate chain and hostname
- native OGL-FED credentials are never reused

---

## Shared Hypergrid HTTP/HTTPS client

A reusable compatibility HTTP client was added for callbacks to remote Hypergrid services.

Responsibilities:

- HTTP POST callback support
- HTTPS/TLS support
- certificate-chain verification
- hostname verification
- cross-platform socket behavior
- bounded request/response handling

It is used for operations such as HomeURI verification and outbound Hypergrid messaging.

---

## Read-only XInventory compatibility

OpenGenesisLINK exposes an OpenSimulator-compatible `/xinventory` endpoint.

The 4.5 milestone supports read operations such as:

- root folder lookup
- inventory skeleton retrieval
- folder contents
- folder/item lookup
- item enumeration
- Asset permission representation

The adapter maps the legacy request onto the native OpenGenesisLINK InventoryStore and Asset permission model.

### Important boundary

XInventory is intentionally **read-only** in this milestone.

Writable remote inventory operations are not yet declared production-ready. The wiki should distinguish clearly between:

- native OpenGenesisLINK Inventory: persistent and writable
- Hypergrid XInventory adapter: compatibility/read-only in 4.5/5.0

---

## Hypergrid AvatarService / Appearance exchange

The compatibility gateway exposes legacy AvatarService behavior through `/avatar`.

Supported data includes:

- AvatarHeight
- VisualParams
- wearables
- attachments
- export-safe Asset references

Native Appearance persistence was expanded while retaining compatibility with older stored records.

### getavatar

`getavatar` serializes native OpenGenesisLINK Appearance information into the legacy AvatarService representation.

Only data that is safe to expose through the compatibility layer is returned.

### setavatar

A guarded legacy `setavatar` path can import Appearance information when the referenced local Inventory/Assets pass ownership and validation rules.

This is not an unrestricted foreign Asset injection mechanism.

---

## Hypergrid return-home lifecycle

Home-grid travel sessions now have an explicit return lifecycle.

Implemented concepts include:

- persistent outbound home-grid travel session
- `get_home_region`
- returning-home state
- authenticated Core return-home control
- `agent_is_coming_home`
- logout/session cleanup

This provides the control-plane lifecycle required for a visitor to return to the originating Grid.

### Still open

The complete legacy Viewer/Simulator data plane remains separate from this lifecycle work.

---

## Core Hypergrid APIs added/expanded

Important authenticated development endpoints include:

```text
GET  /v1/hypergrid/info
POST /v1/hypergrid/travel/issue
POST /v1/hypergrid/travel/return
POST /v1/hypergrid/im/send
GET  /v1/hypergrid/sessions
```

Legacy compatibility endpoints on the dedicated Hypergrid listener include:

```text
/hgfriends
/assets/<legacy-uuid>
/assets/<legacy-uuid>/data
/assets/<legacy-uuid>/metadata
/xinventory
/avatar
/foreignagent
```

Relevant XML-RPC methods include:

- link_region
- get_region
- get_server_urls
- verify_agent
- verify_client
- agent_is_coming_home
- logout_agent
- grid_instant_message
- get_home_region

---

# 5.0.0-dev — Native Script VM

## Design goal

OpenGenesisLINK now has a native server-side Script VM foundation.

It is deliberately small and deterministic. It is **not** a mechanism for running arbitrary native code.

Scripts have no direct:

- filesystem access
- process execution
- raw socket access
- database access

Runtime effects leave the VM as explicit host actions. The host remains responsible for authorization and applying those actions.

---

## Script source format v1

The first native source format is line-oriented and event based.

Example:

```text
event touch
set count 1
add count 2
emit $count
state active
timer 1500
listen 7
end
```

Current instructions:

- `set <name> <value>`
- `add <name> <integer>`
- `emit <value>`
- `state <name>`
- `timer <milliseconds>`
- `listen <channel>`
- `stop`

A value beginning with `$` reads a persistent Script variable.

Multiple event handlers may exist in one Script source.

---

## Sandboxing and runtime budgets

Every Script event is constrained.

Current limits include:

- instruction budget
- maximum variable count
- maximum persistent VM-state size
- maximum number of emitted host actions
- source size limit
- handler/instruction count limits

If execution exceeds a budget, execution fails without committing the new VM state.

This prevents a single Script from running indefinitely or growing state without bounds.

---

## Persistent Script Runtime

Script records persist across Core restarts.

Persisted fields include:

- Script ID
- object ID
- owner user ID
- SHA-256 source hash
- Script source
- serialized VM state
- logical state
- timer interval
- next timer timestamp
- listen channel
- listen enabled/disabled state
- event counter

Older Script Runtime records from the earlier foundation remain readable.

---

## Timer execution

The Core maintenance loop checks due Script timers.

A due timer:

1. is detected by the persistent Script Runtime
2. creates a timer event
3. executes the matching VM event handler
4. persists the updated VM state
5. applies allowed host actions such as timer/listen-state changes

This makes timer state restart-safe.

---

## Script Core API

Authenticated development endpoints:

```text
GET  /v1/scripts
POST /v1/scripts
POST /v1/scripts/event
```

### POST /v1/scripts

Creates a Script associated with an object and authenticated owner.

The source is compiled/validated before the program becomes usable.

The runtime stores the SHA-256 source hash.

### GET /v1/scripts

Returns Scripts owned by the authenticated user.

### POST /v1/scripts/event

Executes one named event against a Script owned by the authenticated user.

The response can contain:

- instructions executed
- resulting Script state
- explicit host actions

---

## Script VM host actions

Current VM actions include:

- emit
- state change
- timer configuration
- listen-channel configuration

This API boundary is important for future expansion. World operations should be added as explicit capabilities/actions rather than exposing unrestricted C++ or operating-system functions to Scripts.

---

## Script roadmap

Not complete yet:

- full LSL compatibility
- richer Scene/Object API
- HTTP-out
- Inventory functions
- Group functions
- Parcel/Land functions
- runtime permission prompts
- distributed World-Node Script execution
- complete Script migration for moving physical objects
- production profiling and per-owner/per-Region quotas

The native VM is the base on which these can be implemented.

---

# 5.0.0-dev — Transactional Region Crossing

## Previous behavior

Adjacent-Region handoff already existed, but the final connection switch was primarily client coordinated.

The early CrossingStore foundation carried position and velocity but was not fully integrated into the handoff ticket flow.

---

## New transactional flow

5.0 integrates the Crossing transaction into Viewer handoff.

Flow:

1. authenticated user requests `POST /v1/viewer/handoff`
2. Core validates source/destination Regions
3. adjacency is checked
4. moderation rules are checked
5. Estate policy is checked
6. destination Parcel entry is checked
7. Core prepares a short-lived Crossing record
8. runtime state is captured
9. Core issues a destination Scene Ticket containing the Crossing ID
10. destination Scene validates the signed ticket
11. Scene join exposes the signed Crossing ID
12. client completes the transaction using `POST /v1/viewer/handoff/complete`
13. Core atomically marks the Crossing completed
14. the transferred runtime state is returned

---

## Crossing state

A Crossing record contains:

- Crossing ID
- user ID
- source Region
- destination Region
- destination position
- velocity
- Avatar Appearance / attachment context
- owned Script IDs
- logical Script states
- serialized Script VM states
- prepared/completed/aborted state
- creation time
- expiry
- completion time

Opaque runtime-state payloads are size bounded before persistence.

---

## Scene Ticket binding

The Crossing ID is part of the HMAC-signed Scene Ticket.

This prevents a client from replacing the Crossing ID with another unsigned transaction while keeping an otherwise valid Scene Ticket.

The destination Scene returns the validated Crossing ID as part of Scene join state.

---

## One-time completion and replay protection

`POST /v1/viewer/handoff/complete` verifies:

- authenticated user
- Crossing ID
- expected destination Region
- prepared state
- expiry

A completed Crossing cannot be completed again.

Mismatch conditions return an error rather than silently accepting a foreign transaction.

Expired prepared Crossings are removed by maintenance.

---

## Velocity transfer

Crossing preparation accepts and persists velocity components:

- vx
- vy
- vz

The values survive persistence and are returned with the completed Crossing state.

This is groundwork for physically continuous movement between adjacent Regions.

---

## Attachment and Script-state transfer

At handoff preparation Core captures the authenticated Avatar Appearance context, including attachment references.

Core also captures owned Script runtime state:

- Script ID
- object ID
- logical state
- serialized VM state

The destination completion response contains this runtime context.

Future server-to-server object migration can consume the same transaction model without trusting arbitrary Viewer-provided state.

---

## Handoff APIs

```text
POST /v1/viewer/handoff
POST /v1/viewer/handoff/complete
```

The handoff response now includes a `crossing_id`.

The destination Scene join acknowledgement also exposes the ticket-validated `crossing_id`.

---

## Crossing security properties

Implemented:

- short expiry
- one-time consumption
- authenticated user binding
- destination Region binding
- state-machine enforcement
- signed Scene-Ticket binding
- persisted transaction state
- bounded transfer payloads
- automatic expiry cleanup

---

## Current boundary

The Viewer still coordinates the network connection switch from one Scene endpoint to another.

Not yet implemented:

- transparent server-to-server socket/session migration
- full physical object transfer between World Nodes
- complete distributed Script ownership migration
- distributed transaction recovery across multiple Core instances
- rollback of a destination Scene after a post-join transport failure

The important change in 5.0 is that runtime state is now protected by a Core transaction instead of existing only as an unsigned client-side handoff hint.

---

# Persistence changes

New/default Core storage paths:

```toml
[storage]
crossings = "data/crossings.db"
scripts = "data/scripts.db"
```

## Script store

Script persistence format advanced to v2 to include source and VM state.

## Crossing store

Crossing persistence format advanced to v2 to include attachment and Script-state payloads.

The loaders retain compatibility with earlier records from the previous foundation formats.

---

# API discovery / capabilities

5.0 advertises new Core capabilities:

- `script-vm-v1`
- `crossing-v2`

Relevant API discovery entries include:

- `GET|POST /v1/scripts`
- `POST /v1/scripts/event`
- `POST /v1/viewer/handoff`
- `POST /v1/viewer/handoff/complete`

---

# Metrics

Core metrics now include counts for:

- persistent Scripts
- Region Crossing records

These extend the existing Core, Region, Presence, content, federation and Hypergrid operational metrics.

---

# Test and build status

The 5.0 milestone passed:

- Linux x86_64 Release build
- Linux x86_64 warnings-as-errors
- Linux x86_64 unit tests
- Linux x86_64 integrated process smoke
- Linux ARM64/aarch64 Release build
- Linux ARM64 warnings-as-errors
- Linux ARM64 unit tests
- Linux ARM64 integrated process smoke
- Windows x86_64 MSVC build
- Windows warnings-as-errors
- Windows CTest/unit tests

The integrated smoke verifies the new Script VM API and transactional Crossing prepare/join/complete/replay behavior.

---

# Related documentation

The wiki may use these repository documents as deeper technical references:

- `README.md`
- `CHANGELOG.md`
- `docs/SCRIPT-RUNTIME-v1.md`
- `docs/REGION-CROSSING-v1.md`
- `docs/HYPERGRID-COMPAT-v0.md`
- `docs/OGL-FED-v0.md`
- `docs/SCENE-PROTOCOL-v0.md`
- `docs/REGION-HANDOFF-v0.md`
- `docs/PRESENCE-SOCIAL-v1.md`
- `docs/CONTENT-SERVICES-v0.md`
- `docs/PERMISSIONS-v1.md`

---

# Suggested wiki topic distribution

The wiki is free to reorganize this material. Possible destinations:

- **Server / Architecture** — Core/World responsibilities and runtime boundaries
- **Scripting** — Script VM language, persistence, limits and roadmap
- **Regions / Handoff** — transactional Crossing flow
- **Viewer Protocol** — Scene Ticket Crossing-ID behavior
- **Hypergrid Compatibility** — IM, Inventory, Appearance and return-home additions
- **Inventory & Assets** — XInventory compatibility and export controls
- **Avatar System** — AvatarService Appearance exchange
- **Security** — VM sandboxing, Crossing replay protection, TLS callback validation
- **API Reference** — new Script and Handoff endpoints
- **Configuration** — new storage paths
- **Operations / Monitoring** — new metrics
- **Development Status / Changelog** — 4.5 and 5.0 milestone summaries
- **Roadmap** — remaining Script, Crossing and Hypergrid work

---

# Statements the wiki should avoid

Do not state that OpenGenesisLINK currently provides:

- complete LSL compatibility
- production-stable Script execution
- fully writable Hypergrid XInventory
- completely transparent Region migration
- complete physical-object crossing
- completed OpenSimulator Viewer/Simulator data-plane compatibility
- production-ready multi-Core distributed Crossing transactions

The implemented features are substantial development foundations, but `5.0.0-dev` remains a development milestone.
