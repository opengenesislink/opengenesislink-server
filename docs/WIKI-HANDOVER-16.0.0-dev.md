# Wiki Handover — OpenGenesisLINK Server 16.0.0-dev

## Milestone

OpenGenesisLINK Server 16.0.0-dev is the **Script & LSL Event Expansion** milestone.

- repository: `opengenesislink/opengenesislink-server`
- pull request: #22
- branch: `dev/16.0.0-script-lsl-expansion`
- version: `16.0.0-dev`
- implementation: C++23
- license: MPL 2.0
- verified pre-documentation code checkpoint: `9100b31a167a7e464145681cbd0d017366aff21f`
- final tested PR head: `d8c78aaf58fa03e58692a525366a423206e7e760`
- squash merge: `cf99988f37d0c3abad20454f017afb28ab008588`

16.0 builds on the 15.0 Platform Services milestone. The main focus is no longer adding another platform service; it is making the ScriptEngine react to more real World/Scene activity so LSL compatibility advances from parser/catalog coverage toward actual runtime semantics.

## Major additions

### LSL function expansion

The current machine-readable function matrix is:

- total canonical functions: 523
- implemented: 62
- partial: 34
- recognized: 402
- unsupported: 25
- strictly implemented: 11.85%
- executable including partial: 18.36%

The exact command-by-command matrix is:

`docs/SCRIPT-COMMAND-STATUS-16.0.md`

### Automatic state lifecycle

State transitions now generate the expected lifecycle around the change:

```text
current state
   |
   +--> state_exit
   |
   +--> switch VM state
   |
   +--> state_entry
```

The lifecycle runs through the shared Script VM and ScriptHost instead of bypassing the normal policy/action path.

### Collision and land-collision events

Region Runtime collision contacts are connected to object-bound Script events:

- `collision_start`
- `collision`
- `collision_end`
- `land_collision_start`
- `land_collision`
- `land_collision_end`

These are generated from the native Physics/Region Runtime event stream and forwarded World → Core → Script Runtime.

### Authenticated Scene touch pipeline

16.0 introduces a native Scene interaction contract for Viewer/object interaction.

New protocol messages:

- `entity_interact = 128`
- `entity_interact_ack = 129`

New default Scene-ticket capability:

- `scene.object.interact`

Accepted phases:

- `start` → `touch_start`
- `touch` → `touch`
- `end` → `touch_end`

The World Node verifies the Scene ticket capability, target object, authenticated Avatar entity and user binding before a Scene event is created. The Viewer cannot directly inject an arbitrary Script event through this path.

### First native changed bitmasks

16.0 connects two real object mutations to LSL `changed(integer change)`:

| Source | LSL mask | Value |
|---|---|---:|
| object scale mutation | `CHANGED_SCALE` | `0x008` / 8 |
| link or unlink mutation | `CHANGED_LINK` | `0x020` / 32 |

Scale events are emitted only when the scale actually changes.

Link/unlink events are emitted for affected linkset members so scripts in the linkset receive the topology change.

## LSL event progress

The current event matrix is:

- total event/catalog entries: 44
- implemented: 1
- partial: 13
- recognized: 29
- unsupported: 1
- strictly implemented: 2.27%
- executable including partial: 31.82%

The executable event set now includes the existing timer/state/listen path plus automatic state lifecycle, collisions, land collisions, touch and changed sources documented in the 16.0 status matrix.

## API and protocol discovery

16.0 adds discovery markers for:

- `scene-interaction-v1`
- `lsl-touch-events-v1`
- `lsl-changed-events-v1`

The Scene hello object-runtime declaration also advertises `interaction-v1`.

## Regression hardening

The 15.0 Platform Services process smoke previously asserted a fixed `15.0.0` API version. 16.0 changes it to derive the expected version from the repository `VERSION` file.

This keeps the completed Platform Services regression suite active on future development branches instead of making every version bump fail for a non-functional reason.

## Tests added or expanded

Cross-platform C++ coverage now verifies:

- default Scene tickets include `scene.object.interact`
- valid touch start/hold/end sequences create ordered Scene events
- mismatched Avatar identity is rejected
- invalid interaction phases are rejected
- real scale mutation emits `changed` with mask 8
- real link mutation emits `changed` with mask 32 for both affected linkset members
- the 16.0 LSL function/event status matrices match the implementation catalogs

The Linux x86_64 and ARM64 ScriptEngine process smoke now verifies:

- Core + World startup
- real Scene object creation
- authenticated touch start/hold/end
- delivery to LSL handlers
- real Scene scale update
- automatic `changed` delivery
- existing collision and land-collision delivery
- the version-aware Platform Services regression smoke

## Acceptance result

All required gates passed on final PR head `d8c78aaf58fa03e58692a525366a423206e7e760`:

- Linux x86_64 warnings-as-errors build: PASS
- Linux x86_64 unit tests: PASS
- Linux x86_64 World/Social/Handoff smoke: PASS
- Linux x86_64 Script World query/ACK smoke: PASS
- Linux x86_64 OGL/LSL ScriptEngine smoke: PASS
- Linux x86_64 Crossing v3 smoke: PASS
- Linux x86_64 Object Crossing smoke: PASS
- Linux x86_64 Viewer Bootstrap/Scene v2 smoke: PASS
- Linux x86_64 OGL-FED/2 Remote Services smoke: PASS
- Linux x86_64 Platform Services smoke: PASS
- Linux ARM64/aarch64 warnings-as-errors build: PASS
- Linux ARM64/aarch64 unit tests: PASS
- Linux ARM64/aarch64 World/Social/Handoff smoke: PASS
- Linux ARM64/aarch64 Script World query/ACK smoke: PASS
- Linux ARM64/aarch64 OGL/LSL ScriptEngine smoke: PASS
- Linux ARM64/aarch64 Crossing v3 smoke: PASS
- Linux ARM64/aarch64 Object Crossing smoke: PASS
- Linux ARM64/aarch64 Viewer Bootstrap/Scene v2 smoke: PASS
- Linux ARM64/aarch64 OGL-FED/2 Remote Services smoke: PASS
- Linux ARM64/aarch64 Platform Services smoke: PASS
- Windows x86_64/MSVC warnings-as-errors build: PASS
- Windows unit tests: PASS
- SQLite integration: PASS
- PostgreSQL integration: PASS
- MariaDB integration: PASS

Canonical 16.0 squash merge:

`cf99988f37d0c3abad20454f017afb28ab008588`

## Explicit non-claims

16.0 does not claim:

- complete Second Life LSL compatibility
- complete `llDetected*` touch/collision metadata
- all `CHANGED_*` bitmasks
- attach/detach lifecycle
- permissions lifecycle
- `link_message` / full linked-message semantics
- sensor/no_sensor runtime
- HTTP/DataServer runtime
- Inventory/Asset API completeness from LSL
- animations, media, vehicle or experience parity
- a stable 1.0 Script/Scene protocol

## Next development targets after 16.0

The highest-value next ScriptEngine targets are:

1. `link_message` plus `llMessageLinked`
2. sensor/no_sensor with bounded Region queries
3. attach/detach and attachment-aware lifecycle
4. permissions/run_time_permissions
5. detected-event context so `llDetectedKey`, `llDetectedName`, touch face/UV and collision metadata can be implemented correctly
6. HTTP/DataServer async request/result model

These should continue to use server-authoritative World/Scene sources rather than unrestricted Script access to Region Runtime internals.
