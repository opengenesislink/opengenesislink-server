# Script World Actions v1

OpenGenesisLINK 6.0.0-dev introduces the first native Script-to-World action path without giving the Script VM direct access to a RegionRuntime or World Node.

## Architecture

```text
Script VM
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
OGL Core/World control connection
  |
  v
owning World Node
  |
  v
RegionRuntime policy validation
```

The Core and World processes remain separate. A Script does not receive a RegionRuntime pointer, raw network socket, filesystem access or arbitrary Core service access.

## Object binding

World-capable Script records use the existing Script `object_id` field with this development binding:

```text
<region-id>/<numeric-entity-id>
```

Example:

```text
genesis-central/42
```

The binding is parsed by ScriptHost. The World Node independently verifies that:

- the requested Region is hosted by that connected World Node
- the target entity exists
- the target entity is an object
- the object's owner matches the Script owner

An invalid or stale binding is rejected.

## Script instructions

### Transform

```text
move <x> <y> <z>
rotate <x> <y> <z>
scale <x> <y> <z>
```

The VM emits typed actions. Numeric parsing and application happen at the World Node. Transform changes are applied through RegionRuntime rather than direct Scene storage access.

### Physics

```text
physics 0
physics 1
```

The World Node can remove or create the object's physics body through RegionRuntime.

### Local chat

```text
say <text>
whisper <text>
shout <text>
```

These become Region scene events. Text is limited by the Script compiler and RegionRuntime event path.

## Core/World transport

The OGL wire foundation now includes:

- `script_action_poll`
- `script_action`

A connected World Node polls actions only for its own registered Regions. Core verifies Region ownership against the node ID and generation before returning an action.

The queue is bounded by:

```toml
[scripting]
max_pending_world_actions = 4096
```

The implementation clamps the queue limit to a safe supported range.

## Current security properties

- no direct VM-to-World pointer
- no raw network capability exposed to Scripts
- typed action allowlist
- bounded pending-action queue
- Region-to-node ownership check in Core
- object existence/type check in World
- Script owner must match object owner
- Script source/action size limits remain active

## Current limitations

This is the first World-action foundation, not the complete Script World API.

Not yet implemented:

- Script-side object read/query instructions
- nearby-avatar queries
- terrain/water queries
- set text/light/sound/material actions
- persistent delivery queue
- World-action acknowledgement/retry transaction
- per-Region/per-owner action quotas
- permission-dialog grants for privileged actions
- distributed multi-Core queue coordination
- full LSL compatibility

A World action is currently removed from the Core queue when delivered to the World Node. A rejected action is logged by the World Node but is not retried.

## Testing

6.0 tests cover:

- compilation of transform, physics and chat actions
- ScriptHost binding of actions to Region/entity/owner
- bounded queue routing
- integrated Core-to-World delivery
- real Scene object movement verified through a Scene snapshot
