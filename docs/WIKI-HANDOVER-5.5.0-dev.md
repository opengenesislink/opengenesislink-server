# OpenGenesisLINK Wiki Handover — 5.5.0-dev

> Purpose: source material for the OpenGenesisLINK wiki.
>
> This document is intentionally organized as a handover rather than one final wiki page. The wiki may distribute the material across existing or new subpages.

## Metadata

- Project: OpenGenesisLINK Server
- Version: `5.5.0-dev`
- Milestone merge commit: `5bf332350e9356a79604981647c85d4c079b7672`
- Language: C++23
- License: MPL 2.0
- Native federation: `OGL-FED/1`
- Hypergrid: separate legacy compatibility layer
- CI targets: Linux x86_64, Linux ARM64/aarch64, Windows x86_64/MSVC

---

# Executive summary

The 5.5 milestone extends two areas:

1. The native Script VM gains a policy-controlled ScriptHost layer. Scripts can request owner Notifications and local direct messages without receiving direct access to Core stores or operating-system services.
2. The Hypergrid XInventory adapter gains guarded write support. Writes are disabled by default and, when explicitly enabled, map legacy OpenSimulator folder/item operations into the native InventoryStore with ownership, hierarchy and Asset-permission checks.

The native InventoryStore was upgraded to persistence format v2 so legacy folder/item UUIDs remain stable across restarts.

---

# ScriptHost v1

## Architecture

The Script VM remains sandboxed.

A Script does not receive direct access to:

- filesystem
- processes
- raw sockets
- databases
- MessageStore
- NotificationStore
- FriendsStore
- arbitrary Core services

Instead, the VM emits typed `ScriptAction` records. ScriptHost receives those actions after execution and decides whether each action is allowed.

This separates language execution from authorization.

## New Script instructions

### notify

```text
notify Your scripted event completed
```

The VM emits a `notify_owner` action.

ScriptHost creates a native OpenGenesisLINK Notification for the Script owner.

Properties:

- Notification type: `script`
- title: `Script notification`
- body: requested text
- target: Script ID

Normal Notification sanitization and persistence remain active.

### message

```text
message <recipient-user-id> Hello from this Script
```

The VM emits a `direct_message` action.

ScriptHost accepts it only when:

- the recipient is an existing local OpenGenesisLINK user
- the recipient is an accepted friend of the Script owner
- MessageStore accepts the message

If accepted:

- message is persisted through the native MessageStore
- recipient receives a native `direct_message` Notification

Unknown users and non-friends are rejected.

## API execution

`POST /v1/scripts/event` now performs two stages:

1. execute the sandboxed VM event
2. pass emitted actions to ScriptHost

The response includes:

- VM actions
- `host_applied`
- `host_errors`

This makes policy rejection visible without granting broader Script permissions.

## Timer execution

Automatic timer events use the same ScriptHost path.

Flow:

1. Core maintenance detects a due timer
2. Script Runtime executes the timer event
3. updated VM state is persisted
4. emitted host actions are passed to ScriptHost
5. ScriptHost applies or rejects them using the same policy as manual events

There is no privileged timer-only bypass.

## Security properties

Implemented:

- VM instruction budget
- variable count limit
- state-size limit
- output-action limit
- source-size and handler-size limits
- no direct OS access
- no direct Core-store access
- explicit typed host actions
- local user validation for messages
- friendship requirement for Script messages

## Remaining Script work

Not complete yet:

- full LSL compatibility
- HTTP-out
- Inventory mutation from Scripts
- Group mutation from Scripts
- Parcel/Land mutation from Scripts
- richer Scene/Object host actions
- permission-dialog workflow
- per-owner/per-Region production quotas
- distributed World-Node Script execution

---

# Hypergrid XInventory v2

## Default behavior

Hypergrid Inventory writes remain disabled by default.

```toml
[hypergrid]
inventory_write_enabled = false
```

Existing read compatibility remains available when Hypergrid is enabled.

To enable legacy write support explicitly:

```toml
[hypergrid]
inventory_write_enabled = true
```

This should currently be used only on a trusted compatibility deployment.

## Supported read methods

- `CREATEUSERINVENTORY`
- `GETROOTFOLDER`
- `GETINVENTORYSKELETON`
- `GETFOLDERCONTENT`
- `GETFOLDERITEMS`
- `GETFOLDER`
- `GETITEM`
- `GETASSETPERMISSIONS`

## Supported write methods

When `inventory_write_enabled = true`:

- `ADDFOLDER`
- `UPDATEFOLDER`
- `MOVEFOLDER`
- `DELETEFOLDERS`
- `PURGEFOLDER`
- `ADDITEM`
- `UPDATEITEM`
- `MOVEITEMS`
- `DELETEITEMS`

The adapter understands OpenSimulator form-list encoding such as repeated:

- `FOLDERS[]`
- `ITEMS[]`
- `IDLIST[]`
- `DESTLIST[]`

## Native Inventory mutation support

InventoryStore now supports native mutation operations required by the adapter:

- folder create
- folder update
- folder move
- folder delete
- folder purge
- item create
- item update
- item move
- item delete
- folder lookup
- item lookup

Folder move/update checks prevent:

- wrong-owner parent folders
- self-parenting
- descendant cycles
- root-folder mutation through ordinary folder operations

## Stable legacy UUID aliases

OpenSimulator expects folder and item UUIDs to remain stable.

Native OpenGenesisLINK IDs are independent from those legacy UUIDs.

InventoryStore v2 therefore adds:

```text
InventoryFolder.legacy_id
InventoryItem.legacy_id
```

Behavior:

- entries created through XInventory preserve the exact supplied legacy UUID
- native entries without an alias continue to use deterministic compatibility mapping
- aliases persist across Core restart
- aliases remain scoped to the local owner
- old Inventory v1 records still load

This prevents a remote OpenSimulator from seeing different Inventory IDs after a restart.

## Asset validation for item writes

A remote XInventory item cannot reference an arbitrary foreign Asset and have it silently accepted.

For `ADDITEM` and `UPDATEITEM`, OpenGenesisLINK resolves the legacy Asset UUID to a local Asset owned by the same local user.

The Asset must pass:

- local ownership validation
- `Export` permission
- `Transfer` permission

If the Asset does not satisfy those requirements, the write is rejected.

## Folder validation

Folder operations validate:

- local owner
- existing parent
- parent owned by the same user
- no self-parenting
- no descendant cycles
- root folder protected from normal update/move/delete

## Security warning

The legacy OpenSimulator XInventory form protocol does not carry the same authorization envelope as native OpenGenesisLINK APIs.

Therefore 5.5 intentionally keeps write mode opt-in.

Current recommendation when writes are enabled:

- bind the Hypergrid listener to a trusted interface where possible
- restrict exposure with firewall/reverse proxy rules
- do not treat the writable legacy endpoint as equivalent to native authenticated APIs

Stronger service-to-service authentication remains production hardening work.

---

# Inventory persistence v2

The Inventory persistence format was upgraded.

New persisted fields:

- optional folder legacy UUID
- optional item legacy UUID

Compatibility:

- v1 Inventory files remain readable
- v2 writes include aliases
- native IDs remain unchanged
- legacy aliases survive restart

---

# Capability discovery

5.5 advertises:

- `script-vm-v1`
- `script-host-v1`
- `crossing-v2`
- `hypergrid-xinventory-v2`

The XInventory capability indicates implementation support. Actual write availability still depends on `hypergrid.inventory_write_enabled`.

---

# Configuration changes

New Hypergrid option:

```toml
[hypergrid]
inventory_write_enabled = false
```

Default: `false`.

No change is required for installations that want to remain read-only.

---

# Test coverage

The 5.5 milestone adds tests for:

## XInventory

- writes rejected by default
- opt-in ADDFOLDER
- second folder creation
- ADDITEM
- MOVEITEMS
- UPDATEFOLDER
- persisted legacy folder UUID after restart
- persisted legacy item UUID after restart
- DELETEITEMS
- DELETEFOLDERS
- Asset Export/Transfer validation through the adapter

## ScriptHost

- Script `notify` compilation/execution
- Script `message` compilation/execution
- owner Notification creation
- accepted-friend direct message creation
- recipient Notification creation
- host applied/error reporting

## Integrated smoke

The Linux process smoke now includes:

- writable XInventory enabled explicitly in test configuration
- remote-compatible folder creation and lookup
- Script notification host action
- Script friend-message host action
- persistent message counts through Core restart
- all previous federation, Hypergrid, social, governance, Scene and Crossing tests

---

# Build status

5.5.0-dev passed:

- Linux x86_64 build with warnings-as-errors
- Linux x86_64 unit tests
- Linux x86_64 integrated process smoke
- Linux ARM64/aarch64 build with warnings-as-errors
- Linux ARM64 unit tests
- Linux ARM64 integrated process smoke
- Windows x86_64 MSVC build with warnings-as-errors
- Windows CTest/unit tests

---

# Related repository documentation

- `README.md`
- `CHANGELOG.md`
- `docs/SCRIPT-RUNTIME-v1.md`
- `docs/SCRIPT-HOST-v1.md`
- `docs/HYPERGRID-COMPAT-v0.md`
- `docs/HYPERGRID-XINVENTORY-v1.md`
- `docs/REGION-CROSSING-v1.md`
- `docs/CONTENT-SERVICES-v0.md`
- `docs/PERMISSIONS-v1.md`

---

# Suggested wiki distribution

Possible wiki destinations:

- **Scripting / Script VM** — new language instructions
- **Scripting / Security** — VM/ScriptHost separation
- **Scripting / Social APIs** — Notification and friend messaging behavior
- **Hypergrid / Inventory** — XInventory v2 methods
- **Hypergrid / Security** — write-mode opt-in and trusted-network warning
- **Inventory / Persistence** — legacy aliases and format v2
- **Permissions** — Export/Transfer checks for HG item writes
- **Configuration** — `inventory_write_enabled`
- **API / Capabilities** — `script-host-v1`, `hypergrid-xinventory-v2`
- **Testing** — new cross-platform coverage
- **Development Status / Changelog** — 5.5 milestone

---

# Statements the wiki should avoid

Do not claim that 5.5 provides:

- unrestricted remote Hypergrid Inventory writes by default
- production-grade service authentication for writable XInventory
- arbitrary foreign Asset import
- full LSL compatibility
- arbitrary Script access to Core services
- Script filesystem/process/socket access
- complete OpenSimulator Viewer/Simulator data-plane compatibility
- finished production security for the entire Hypergrid layer

5.5.0-dev remains a development milestone.
