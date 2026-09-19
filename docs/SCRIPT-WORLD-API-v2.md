# Script World API v2

OpenGenesisLINK 6.5.0-dev upgrades the 6.0 Script World action foundation with durable delivery, acknowledgement/retry semantics and asynchronous read/query operations.

## Delivery model

Script World actions are persisted by Core before delivery.

Each queued record stores:

- action ID
- Region ID
- target entity ID
- Script owner
- Script ID
- action type and payload
- creation and expiry timestamps
- lease deadline
- delivery-attempt counter
- last error

The queue is file-backed through `storage.script_world_actions`.

## Lease and acknowledgement flow

```text
ScriptHost
   |
   v
durable queue
   |
   | lease
   v
World Node
   |
   | apply/query
   v
script_action_result
   |
   v
Core
   |
   +-- ACK  -> delete action
   |
   +-- NACK -> retry after delay
```

A poll no longer removes an action.

Core leases it for a bounded interval. If the World Node disconnects before acknowledging the result, the lease eventually expires and the action becomes eligible for another attempt.

Actions are removed when:

- Core receives a successful result and ACKs it
- the configured TTL expires
- the maximum delivery-attempt budget is exhausted

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

Runtime clamps remain active to prevent unbounded values.

## Wire additions

The Core/World control channel now uses:

- `script_action_poll`
- `script_action`
- `script_action_result`
- `script_action_result_ack`

Core still validates that the polling node owns the requested Region in its current node generation.

Result messages are also Region-bound before Core accepts them.

## Query opcodes

Queries remain typed Script actions. The VM never receives a direct RegionRuntime pointer.

### object_info

```text
object_info obj
```

After asynchronous completion, VM variables include:

- `obj.ready`
- `obj.name`
- `obj.position`
- `obj.rotation`
- `obj.scale`
- `obj.physical`
- `obj.group`
- `obj.owner_permissions`
- `obj.group_permissions`
- `obj.everyone_permissions`

The bound object must exist and must be owned by the Script owner.

### region_info

```text
region_info region
```

Result variables include:

- `region.ready`
- `region.id`
- `region.terrain_width`
- `region.terrain_height`
- `region.terrain_revision`
- `region.entity_count`
- `region.avatar_count`
- `region.sim_fps`

### terrain_height

```text
terrain_height ground
```

The terrain is sampled at the bound object's current position.

Result variables include:

- `ground.ready`
- `ground.height`
- `ground.x`
- `ground.y`

### nearby_avatars

```text
nearby_avatars nearby 32
```

The radius is in Region meters and is restricted to 1–96.

Results are distance ordered and capped at 16 Avatars.

Variables include:

- `nearby.ready`
- `nearby.count`
- `nearby.avatar0`
- `nearby.avatar1`
- ...

Each Avatar value contains the Scene entity ID, sanitized display name and distance. Persistent account IDs are not exposed by this query.

## Asynchronous behavior

A query result is not available during the same VM instruction sequence that requested it.

Typical flow:

```text
event touch
object_info obj
end

event timer
emit $obj.position
end
```

Scripts can check `<prefix>.ready` before using a result.

This preserves the process boundary and avoids blocking the deterministic VM on network I/O.

## VM state limits

World results are subject to:

- maximum result payload size
- maximum result field count
- field-size limits
- the existing Script variable-count budget
- the existing VM-state size budget

A result that would exceed the VM state budget is rejected and the action follows the normal retry/drop policy.

## Security model

6.5 retains the 6.0 checks and adds result-channel validation.

Mutation checks include:

- Region/node-generation ownership
- object existence and object type
- Script-owner/object-owner match
- owner Modify permission
- Parcel build permission
- finite numeric transforms
- scale limits

Query checks include:

- Region/node-generation ownership
- object existence
- object ownership
- bounded nearby radius
- capped nearby result count

## Current limitations

Still open:

- permission-dialog grants
- sound/light/material/text mutation APIs
- inventory/group/land mutation from Scripts
- distributed queue coordination across multiple Core instances
- richer event-driven query completion callbacks
- full LSL compatibility

The 6.5 queue is durable for a single Core instance. It is not yet a distributed consensus queue.
