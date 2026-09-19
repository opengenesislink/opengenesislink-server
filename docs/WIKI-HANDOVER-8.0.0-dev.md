# Wiki Handover — OpenGenesisLINK Server 8.0.0-dev

## Milestone

OpenGenesisLINK Server 8.0.0-dev is the Object Runtime, Linkset Crossing v2 and Script World API v3 milestone.

- Pull request: #13
- final tested PR head: `b75f23d9a79197c2ba220e47d631ad5ffa73f02d`
- squash merge: `34b40ed0bda88d9feaae96de9597da44089e4836`
- version: `8.0.0-dev`
- implementation language: C++23
- required targets: Linux x86_64, Linux ARM64/aarch64 and Windows x86_64

## CI evidence

The exact final PR head passed every required gate.

### Linux C++ CI

Run #153 — run ID `35442342463`.

Ubuntu 24.04 x86_64:

- warnings-as-errors configure: PASS
- build: PASS
- all unit tests: PASS
- integrated World/Social/Handoff process smoke: PASS
- Script World query/ACK process smoke: PASS
- Crossing v3 reserve/rollback process smoke: PASS
- Linkset/Object Crossing v2 end-to-end process smoke: PASS

Ubuntu 24.04 ARM64/aarch64:

- warnings-as-errors configure: PASS
- build: PASS
- all unit tests: PASS
- integrated World/Social/Handoff process smoke: PASS
- Script World query/ACK process smoke: PASS
- Crossing v3 reserve/rollback process smoke: PASS
- Linkset/Object Crossing v2 end-to-end process smoke: PASS

### Windows C++ CI

Run #124 — run ID `35442342479`.

Windows Server 2022 x86_64:

- MSVC environment: PASS
- vcpkg binary cache: PASS
- OpenSSL dependency installation: PASS
- MSVC `/WX` configure: PASS
- Core/World/tests build: PASS
- unit tests: PASS

## Object Runtime v1

8.0 adds the first native linked-object data model shared by World Runtime, Scene protocol, persistence, Scripts and Crossing.

Every Scene object can now carry:

- parent entity ID
- link number
- floating text
- physical root ownership
- rotation
- linear velocity
- angular velocity

A linkset root has:

```text
parent_entity_id = 0
link_number = 1
```

Children reference the root and use link numbers starting at 2.

Transfer graphs are bounded to 64 members.

### Link / unlink

`RegionRuntime::link_objects` validates:

- both entities exist
- both are objects
- both are standalone before linking
- both have the same owner
- nested linkset roots are not accepted

A linked child does not retain an independent PhysicsWorld body in Object Runtime v1.

`unlink_object` restores a child to a standalone root but does not implicitly enable physics.

### Linked movement

Root translation is propagated to child positions.

This is a topology/runtime foundation, not yet a complete rigid-body constraint solver. Full child transform rotation around a rotating root, joints and vehicle constraints remain later physics work.

## PhysicsWorld angular motion

Physics bodies now include:

- position
- velocity
- rotation
- angular velocity

The native physics tick integrates angular velocity and normalizes rotation degrees.

RegionRuntime can now explicitly set angular velocity and restores it through persistence and Crossing.

## Floating object text

Objects have bounded floating text up to 512 bytes.

The value is available through:

- RegionRuntime
- Scene snapshot
- Scene mutation
- Script World actions
- Scene persistence
- Object Crossing

## Region water level

World configuration now supports:

```toml
[runtime]
water_height = 20.0
```

The water level is exposed by RegionRuntime, Scene snapshots and Script World queries.

8.0 does not claim fluid dynamics or buoyancy simulation yet.

## Scene protocol additions

New message types:

- `entity_link = 118`
- `entity_link_ack = 119`
- `entity_text = 122`
- `entity_text_ack = 123`
- `entity_motion = 124`
- `entity_motion_ack = 125`

The default Scene capability set adds:

- `scene.object.link`

The Scene hello advertises:

```text
object_runtime=linkset-v1,motion-v1,text-v1
```

Scene snapshots retain their previous fields and append:

- parent entity ID
- link number
- physical flag
- floating text
- linear velocity
- angular velocity

They also expose Region water height.

## Scene persistence v5

`objects.db` v5 records persist:

- existing object identity/transform/permissions
- linear velocity
- angular velocity
- parent entity ID
- link number
- floating text

The loader remains backward-readable for:

- 12-field records
- 13-field records
- 17-field records
- 20-field v4 records
- 26-field v5 records

This allows existing development worlds to move forward without requiring a destructive scene migration.

## Object Crossing v2

Object Crossing now moves complete native root/child linksets rather than only a single Scene object.

### Forward ordering

```text
prepared
  -> source linkset export
exported
  -> destination linkset import ACK
imported
  -> source linkset remove ACK
completed
  -> Script binding commit
```

The source is never removed before destination reconstruction has been acknowledged.

### Linkset snapshot

A v2 snapshot contains:

- source root entity ID
- up to 64 members
- source entity ID for every member
- parent/root relation
- link number
- owner/group
- permission masks
- position/rotation/scale
- physical flag
- linear velocity
- angular velocity
- floating text

The serialized Crossing snapshot is bounded to 256 KiB.

### Destination IDs

The root keeps the deterministic Crossing transfer ID.

Each child receives a deterministic destination ID derived from:

- destination root ID
- source child ID
- link number

This makes destination import retries idempotent.

### Persistent entity map

The destination returns a bounded map:

```text
source-id:destination-id,source-id:destination-id,...
```

Core validates:

- nonzero source/destination IDs
- unique source IDs
- unique destination IDs
- maximum 64 mappings
- maximum encoded map size 16 KiB

ObjectCrossingStore v2 persists the map so it survives Core restart.

The API exposes the map on Crossing records for diagnostics.

## Script binding migration

Script Runtime bindings remain canonical as:

```text
region-id/entity-id
```

After source removal succeeds, Core uses the persisted Crossing entity map to atomically rebind Scripts belonging to the Crossing owner.

Script IDs and VM state do not change.

The rebinding operation is idempotent.

The final 8.0 durability fix forces persistence even for an idempotent retry. This covers a case where an earlier disk write could fail after the in-memory binding had already changed.

Scripts are deliberately not rebound while the destination is only staged/imported and the source remains authoritative.

## Lost destination-import ACK reconciliation

7.5 could safely restore after a known destination import but an `exported` transaction still assumed no destination copy existed.

8.0 closes that ambiguity.

An exported transaction that is explicitly rolled back or expires now transitions to:

```text
cleanup_pending
```

Core commands idempotent destination cleanup before source restoration/final rollback.

This handles the case where destination import actually succeeded but its result ACK was lost.

## ObjectCrossingStore v2

The store now supports:

- larger 256 KiB linkset snapshots
- persistent entity maps
- v2 persistence format
- backward reading of v1 records
- destination cleanup reconciliation for exported/imported expired transactions
- existing bounded command attempts
- 24-hour terminal retention

## Script World API v3

New mutation opcodes:

```text
velocity x y z
angular_velocity x y z
text <value>
```

New queries:

```text
water_level <prefix>
world_time <prefix>
```

`object_info` now additionally exposes:

- velocity
- angular velocity
- parent entity
- link number
- linkset count
- floating text

`region_info` additionally exposes:

- water height

All operations still travel through the durable Core Script World queue. The VM does not receive a RegionRuntime pointer.

Existing lease, ACK/NACK, retry, TTL, ownership, permission and Parcel policy rules remain active.

## Capability discovery

8.0 advertises:

- `script-world-actions-v3`
- `script-world-queries-v2`
- `object-crossing-v2`
- `linkset-runtime-v1`
- `scene-motion-v1`
- `scene-text-v1`
- `scene-persistence-v5`

Older capability identifiers remain advertised where compatibility is intentionally retained.

## Process-level Linkset Crossing test

The Linux Object Crossing smoke now creates:

- physical root object
- nonphysical child object
- native parent/child link
- floating root and child text
- root angular motion

It then crosses the root from one adjacent Region to another and verifies:

- transaction completion
- persistent entity map
- both source members removed
- destination root created
- destination child created
- child references destination root
- link number preserved
- child remains nonphysical
- floating text preserved
- root angular motion preserved

The same process smoke passed on Linux x86_64 and ARM64.

## Windows CI improvement

Windows CI now creates an explicit vcpkg binary-cache directory and caches it through `actions/cache`.

This reduces repeated OpenSSL dependency compilation/download work on subsequent runs while keeping the existing MSVC `/WX` gate.

## Deliberate boundaries after 8.0

Do not claim the following are complete:

- full rigid-body linkset constraint physics
- root rotation propagated through child transforms as a mature rigid-body solver
- joints, hinges or vehicle constraints
- vehicle engine/steering models
- avatar attachment object-graph migration
- object Inventory contents as an atomic transfer graph
- mesh/material state as a complete transferable object graph
- object-attached Asset ownership migration
- distributed Object Crossing coordination across multiple Core instances
- seamless Viewer socket/connection migration
- full LSL compatibility

The current Object Runtime and Crossing transaction model are foundations for those layers.

## Recommended next large server milestone

A suitable next major block is 8.5/9.0 focused on Object Graph / Attachment Runtime:

1. object Inventory graph snapshots with Asset reference validation
2. attachment object graphs bound to Avatar Crossing
3. Script execution pause/resume lifecycle during object/attachment transfer
4. richer Script permissions, grants and per-owner/Region quotas
5. advanced linkset transform/physics semantics
6. stronger distributed transaction/reconciliation primitives
7. additional process smokes for attachment and Inventory rollback paths

Viewer and Atlas remain separate repositories/projects and should not be implemented in the server milestone.
