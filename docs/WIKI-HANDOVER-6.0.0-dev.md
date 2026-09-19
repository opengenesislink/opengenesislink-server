# OpenGenesisLINK Wiki Handover — 6.0.0-dev

> Purpose: source material for the OpenGenesisLINK wiki.
>
> This document is a technical handover. The wiki may distribute the material across existing or new pages instead of publishing it as one page.

## Metadata

- Project: OpenGenesisLINK Server
- Version: `6.0.0-dev`
- Milestone merge commit: `3ddda540d039177724844f8d11fb1a59420873d2`
- Pull request: `#9`
- Primary language: C++23
- License: MPL 2.0
- Native federation: `OGL-FED/1`
- Hypergrid: separate legacy compatibility layer
- CI targets: Linux x86_64, Linux ARM64/aarch64, Windows x86_64/MSVC

---

# Executive summary

OpenGenesisLINK 6.0.0-dev introduces the first native policy-controlled Script-to-World action path while preserving the architectural separation between Core and World Nodes.

Scripts can now request a limited set of object and local-chat operations without receiving a RegionRuntime pointer or arbitrary access to World internals.

The milestone also hardens writable OpenSimulator XInventory compatibility. Write mode remains disabled by default. When explicitly enabled, a shared service secret of at least 24 bytes is now mandatory and every legacy write request must carry the matching `SERVICEKEY`.

6.0 is still a development milestone. It does not represent a complete Script World API, production-final Hypergrid security, or a complete legacy Viewer/Simulator data plane.

---

# Script World Actions v1

## Architecture

The 6.0 path is:

```text
Script
  |
  v
Sandboxed Script VM
  |
  v
typed ScriptAction
  |
  v
ScriptHost policy
  |
  v
bounded Core World-Action queue
  |
  v
Core / World control connection
  |
  v
owning World Node
  |
  v
Region and object policy validation
  |
  v
RegionRuntime
```

Important architectural rule:

The Script VM does not directly access:

- RegionRuntime
- Scene stores
- Core stores
- raw sockets
- filesystem
- processes
- databases

World actions remain explicit typed requests.

## Object binding

World-capable scripts currently use the existing Script `object_id` field in this development form:

```text
<region-id>/<numeric-entity-id>
```

Example:

```text
genesis-central/42
```

ScriptHost parses this binding before enqueueing a World action.

The World Node independently validates the target again before applying it.

---

# New Script instructions

## Move

```text
move <x> <y> <z>
```

Requests an object position change.

## Rotate

```text
rotate <x> <y> <z>
```

Requests an object rotation change.

## Scale

```text
scale <x> <y> <z>
```

Requests an object scale change.

Scale components are constrained on the World side and invalid/non-finite values are rejected.

## Physics

```text
physics 0
physics 1
```

Requests removal or creation of the object's physics body.

## Local chat

```text
say <text>
whisper <text>
shout <text>
```

These produce Region scene events using the native runtime event path.

The current event types are:

- `chat`
- `chat_whisper`
- `chat_shout`

---

# ScriptHost World policy

ScriptHost now supports typed World actions in addition to the 5.5 owner Notification and friend-only direct-message actions.

Before a World action can be queued:

- Script must have a valid Region/entity binding
- entity ID must be non-zero
- action payload must remain inside the bounded queue input limits
- queue capacity must not be exceeded

The default queue limit is:

```toml
[scripting]
max_pending_world_actions = 4096
```

The implementation clamps the configured queue size to a safe supported range.

---

# Core-to-World action delivery

6.0 adds two internal wire message types:

- `script_action_poll`
- `script_action`

A World Node polls Core for actions belonging to one of its Regions.

Core checks:

- Region exists
- Region belongs to the connected node ID
- Region is bound to the current node generation

A stale or unrelated node cannot pull actions for another Region through this path.

When an action is returned, the payload contains:

- action ID
- Region ID
- entity ID
- Script owner
- Script ID
- typed action name
- bounded action payload

---

# World-side validation

Delivery by Core is not enough to authorize the mutation.

The World Node independently validates:

- target Region is local
- entity ID parses correctly
- target entity exists
- target entity is an object
- object owner equals Script owner

For object mutation actions it additionally checks:

- owner has `perm_modify`
- numeric vectors contain finite values
- scale is within supported bounds
- Parcel build policy permits the operation at the target/current position

This means Script object transforms use the same Land-policy concept as normal Scene object modification instead of bypassing Parcel rules.

Physics actions validate:

- exact payload `0` or `1`
- owner modify permission
- Parcel build permission at the object's current position

---

# RegionRuntime additions

6.0 adds native runtime support for:

- enabling object physics
- disabling object physics
- emitting `physics_updated` events
- explicit whisper scene events
- explicit shout scene events

Existing local chat continues to use `chat`.

---

# Script World security properties

Implemented in 6.0:

- VM remains sandboxed
- typed action allowlist
- bounded Core queue
- cryptographically random action IDs
- Region/node-generation binding
- object existence check
- object-type check
- Script-owner/object-owner check
- owner modify permission check
- Parcel build-policy check
- finite transform validation
- bounded scaling
- payload size limits

Not implemented yet:

- durable/persistent action queue
- explicit World ACK before Core removes an action
- automatic retry after rejected/lost delivery
- per-owner action quota
- per-Region action quota
- distributed queue coordination across multiple Core instances
- permission-dialog grants for privileged Script actions

Current delivery is therefore a development foundation rather than a transactional World-command bus.

---

# Hypergrid XInventory authentication

## Default

Writable XInventory remains disabled:

```toml
[hypergrid]
inventory_write_enabled = false
inventory_write_secret = ""
```

Read compatibility remains separate from write authorization.

## Enabling writes

A deployment that deliberately enables legacy writes must configure:

```toml
[hypergrid]
inventory_write_enabled = true
inventory_write_secret = "replace-with-a-strong-service-secret"
```

Requirements:

- service secret must contain at least 24 bytes
- Core rejects write-enabled startup configuration with a shorter secret
- every XInventory write request must provide a matching `SERVICEKEY`
- key comparison uses constant-time OpenSSL comparison

## Security boundary

The shared key is a compatibility-layer credential.

Rules:

- do not reuse OGL-FED Ed25519 credentials
- do not transmit the shared key over cleartext Internet HTTP
- use HTTPS or an authenticated private transport
- continue applying firewall/reverse-proxy restrictions to the HG listener

The service key authenticates the current legacy write path, but is not the final long-term trust model.

Still planned:

- per-grid credentials/signatures
- remote-grid allowlists
- replay-resistant nonce/timestamp envelope
- per-grid write quotas
- stronger distributed audit

---

# XInventory validation retained from 5.5

After service-key validation, writes still pass native checks for:

- local owner
- parent folder integrity
- no folder cycles
- protected root folder
- Asset ownership
- Asset Export permission
- Asset Transfer permission

Stable legacy Inventory UUID aliases remain persisted across restart.

---

# Capability discovery

6.0 advertises the new capabilities:

- `script-world-actions-v1`
- `hypergrid-xinventory-auth-v1`

Existing relevant capabilities remain:

- `script-vm-v1`
- `script-host-v1`
- `crossing-v2`
- `hypergrid-xinventory-v2`

---

# Configuration changes

## Scripting

```toml
[scripting]
max_pending_world_actions = 4096
```

## Hypergrid

```toml
[hypergrid]
inventory_write_enabled = false
inventory_write_secret = ""
```

A blank secret is valid while writes remain disabled.

---

# Integrated smoke coverage

The Linux process smoke now additionally proves the Script-to-World path end to end.

The smoke test:

1. starts Core
2. starts a World Node
3. creates an authenticated user
4. creates a real Scene object
5. binds a Script to `genesis-central/<entity-id>`
6. runs a Script event containing `move` and `say`
7. waits for World Node polling
8. requests a Scene snapshot
9. verifies the object moved to the requested coordinates

The smoke also uses an authenticated XInventory write with `SERVICEKEY`.

---

# Negative XInventory security tests

6.0 tests explicitly verify:

- writable adapter rejects a too-short configured service secret
- write request without `SERVICEKEY` is rejected
- write request with incorrect `SERVICEKEY` is rejected
- authenticated writes continue to pass the existing ownership and permission tests

---

# CI result

The 6.0.0-dev milestone PR passed before merge:

## Linux x86_64

- warnings-as-errors configure: PASS
- build: PASS
- unit tests: PASS
- integrated process smoke: PASS

## Linux ARM64/aarch64

- warnings-as-errors configure: PASS
- build: PASS
- unit tests: PASS
- integrated process smoke: PASS

## Windows x86_64

- MSVC `/WX` configure: PASS
- build: PASS
- unit tests: PASS

PR CI runs:

- C++ CI run `112`
- Windows C++ CI run `83`

---

# Related repository documentation

- `README.md`
- `CHANGELOG.md`
- `docs/SCRIPT-RUNTIME-v1.md`
- `docs/SCRIPT-HOST-v1.md`
- `docs/SCRIPT-WORLD-API-v1.md`
- `docs/OGL-WIRE-FOUNDATION-v0.md`
- `docs/HYPERGRID-COMPAT-v0.md`
- `docs/HYPERGRID-XINVENTORY-v1.md`
- `docs/PERMISSIONS-v1.md`
- `docs/REGION-CROSSING-v1.md`

---

# Suggested wiki distribution

Possible wiki destinations:

- **Scripting / Script VM** — 6.0 opcodes
- **Scripting / ScriptHost** — typed host-policy actions
- **Scripting / World API** — Core-to-World action architecture
- **Scripting / Security** — ownership, Parcel and transform validation
- **World Runtime / RegionRuntime** — physics/chat runtime changes
- **Protocol / Core-World Wire** — Script action poll/delivery
- **Hypergrid / Inventory** — authenticated writable XInventory
- **Hypergrid / Security** — shared-key rules and remaining hardening
- **Configuration** — Script queue and XInventory service key
- **API / Capabilities** — new 6.0 capability names
- **Testing** — cross-platform and integrated smoke coverage
- **Development Status / Changelog** — 6.0 milestone

---

# Important limitations for the wiki

Do not claim that 6.0 provides:

- a complete Script World API
- arbitrary Script access to Scene or Core internals
- object/avatar read/query opcodes
- persistent or guaranteed-delivery Script World actions
- transactional ACK/retry for Script World actions
- unrestricted object mutation
- bypass of Parcel permissions
- production-final Hypergrid authentication
- per-grid signed XInventory requests
- complete OpenSimulator Viewer/Simulator data-plane compatibility
- full LSL compatibility

---

# Next recommended server work

After 6.0, the next coherent blocks should continue the existing roadmap rather than creating parallel architecture.

Recommended order:

1. Script World read/query path
   - object transform/owner/group/permissions
   - Region name/time/water/terrain queries
   - nearby avatar queries
2. Script World delivery hardening
   - ACK
   - retry
   - expiry
   - durable pending-action state
   - quotas
3. Object/Attachment Crossing preparation
   - rotation
   - angular velocity
   - physics state
   - linksets
   - Script VM state ownership migration
   - destination reservation / ACK / rollback
4. Hypergrid security and data-plane continuation
   - per-grid authenticated write policy
   - remote allowlist
   - Seed CAPS
   - Root/Child Agent lifecycle
   - Simulator handshake
   - Viewer circuit handoff
5. later Physics expansion according to the separate Physics roadmap

Viewer, Atlas and hosted Voice remain separate projects.

---

# Milestone status

OpenGenesisLINK Server is now:

```text
6.0.0-dev
```

Milestone merge:

```text
3ddda540d039177724844f8d11fb1a59420873d2
```

This remains a development build and must not be described as production-stable.
