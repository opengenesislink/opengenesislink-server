# Object Runtime v1

OpenGenesisLINK 8.0.0-dev introduces the first native linked-object runtime model shared by Scene access, Script World actions, persistence and Object Crossing.

## Entity topology

Scene objects now carry:

- parent entity ID
- link number
- floating text
- physical-body ownership

A root has `parent_entity_id = 0` and `link_number = 1`.

Children point at the root entity and receive link numbers starting at 2. Nested linkset roots are intentionally not supported in v1.

The runtime bounds transferable linksets to 64 members.

## Linking rules

`link_objects(root, child)` requires:

- both entities exist
- both are Scene objects
- both are unlinked roots
- both have the same owner
- the child is not itself a root with children

A linked child does not retain an independent PhysicsWorld body. Physics ownership belongs to the root for the v1 model.

`unlink_object(child)` restores the child to a standalone root. It does not automatically make the object physical.

## Motion

PhysicsWorld bodies now contain:

- position
- linear velocity
- rotation
- angular velocity

The simulation tick integrates linear and angular motion.

Root translation is propagated to linked children. 8.0 establishes the topology and shared runtime contract; advanced rigid-body constraints, joint solvers and vehicle-specific link physics remain later physics work.

## Floating text

Objects can carry bounded floating text up to 512 bytes.

The same value is available through:

- Scene snapshots
- Scene entity-text mutation
- Script World actions
- persistence
- Object Crossing snapshots

## Water level

RegionRuntime has a configured water height.

Default:

```toml
[runtime]
water_height = 20.0
```

The value is runtime metadata in 8.0 and is exposed to Script World queries and Scene snapshots. Full fluid physics is not implied by this foundation.

## Scene protocol additions

8.0 adds:

- `entity_link` / `entity_link_ack`
- `entity_text` / `entity_text_ack`
- `entity_motion` / `entity_motion_ack`

Link operations require the explicit `scene.object.link` capability.

Object text and motion remain subject to object modify permissions.

## Scene persistence v5

Persistent object records add:

- angular velocity X/Y/Z
- parent entity ID
- link number
- floating text

The loader remains compatible with earlier 12-field, 13-field, 17-field and 20-field object records.

## Current boundaries

Object Runtime v1 does not yet implement:

- physics constraints/joints between linked parts
- full root rotation transform propagation as a rigid-body solver
- vehicle steering/engine models
- object Inventory graphs
- attachment-to-avatar object graphs
- material/mesh state transfer
- distributed multi-Core object ownership consensus
