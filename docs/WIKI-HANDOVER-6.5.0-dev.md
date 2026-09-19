# OpenGenesisLINK Wiki Handover — 6.5.0-dev

> Source material for the OpenGenesisLINK wiki. This document describes the verified 6.5.0-dev server milestone.

## Metadata

- Project: OpenGenesisLINK Server
- Version: `6.5.0-dev`
- Milestone merge commit: `6a483abb16bfd54defe1c22c202465b73d183923`
- Language: C++23
- License: MPL 2.0
- Native federation: OGL-FED/1
- CI: Linux x86_64, Linux ARM64/aarch64, Windows x86_64/MSVC
- Verified PR CI: C++ CI run 116; Windows C++ CI run 87

## Executive summary

6.5 turns the first 6.0 Script-to-World path into a restart-safe delivery system and adds the first read/query side of the native Script World API.

Script World actions are no longer destructively removed when a World Node polls them. Core persists pending actions, leases them to the owning World Node and removes them only after an explicit successful result acknowledgement.

World queries return bounded data through the same Core/World control path and Core writes that data into namespaced persistent Script VM variables.

## Reliable Script World delivery

Implemented:

- file-backed pending-action persistence
- bounded queue size
- per-action delivery lease
- explicit World ACK/NACK result
- retry delay
- bounded attempt count
- TTL/expiry
- restart-safe pending actions
- Region/node-generation ownership check before delivery
- action result accepted only from the node currently owning the Region

The VM still receives no direct RegionRuntime pointer, raw socket, filesystem access or unrestricted Core service access.

## New Core/World messages

6.5 adds:

- `script_action_result`
- `script_action_result_ack`

Flow:

```text
Script VM
  -> ScriptHost
  -> durable Core action queue
  -> World lease/poll
  -> World validation/execution
  -> result ACK/NACK
  -> Core commit/retry
```

## Script World queries

New Script instructions:

```text
object_info <prefix>
region_info <prefix>
terrain_height <prefix>
nearby_avatars <prefix> <radius>
```

Query results are stored as VM variables under the requested prefix.

Example:

```text
object_info obj
```

May create variables such as:

```text
obj.position
obj.rotation
obj.scale
obj.name
obj.physical
obj.owner_permissions
obj.ready
```

The `.ready` variable becomes `1` when Core has persisted the returned query result.

## Query security and bounds

Implemented:

- object query remains bound to the Script's object
- Script owner must match object owner
- query payload/result sizes are bounded
- VM variable budget remains enforced
- nearby-avatar radius is bounded
- nearby-avatar result count is capped
- query data crosses only the existing authenticated Core/World node session
- no arbitrary memory/service reads are exposed

## Configuration

```toml
[storage]
script_world_actions = "data/script-world-actions.db"

[scripting]
max_pending_world_actions = 4096
world_action_max_attempts = 5
world_action_lease_ms = 2000
world_action_ttl_ms = 60000
```

## Capability discovery

Added:

- `script-world-actions-v2`
- `script-world-queries-v1`

Existing relevant capabilities remain:

- `script-vm-v1`
- `script-host-v1`
- `script-world-actions-v1`

## Testing

6.5 verifies:

- leased action survives queue restart
- ACK removes the durable action
- NACK retains the action for retry
- retry delay is enforced
- retry attempt count increments
- query results persist into Script VM state
- integrated Core/World mutation path remains operational
- focused two-process Core+World query/ACK smoke succeeds

Cross-platform milestone result:

- Linux x86_64: build, unit tests and process smoke PASS
- Linux ARM64/aarch64: build, unit tests and process smoke PASS
- Windows x86_64/MSVC: /WX build and unit tests PASS

## Important boundary

6.5 does not claim:

- full LSL compatibility
- arbitrary Scene reads
- arbitrary RegionRuntime access
- distributed multi-Core action queue coordination
- complete object material/light/sound APIs
- production-final per-owner/per-Region Script quotas

## Suggested wiki pages

- Scripting / Script World API
- Scripting / World Queries
- Scripting / Delivery Reliability
- Protocol / Core-World Script Messages
- Configuration / Scripting
- Testing / Cross-platform CI
- Development Status / 6.5.0-dev
