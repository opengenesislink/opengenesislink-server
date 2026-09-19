# Script World API v3

OpenGenesisLINK 8.0.0-dev extends the durable Script World API with object-runtime motion, text and additional readback.

The VM still never receives a direct RegionRuntime pointer. All World operations travel through the durable Core queue with leases, ACK/NACK, retry and expiry.

## New mutation opcodes

### velocity

```text
velocity 1 0 0
```

Sets linear velocity on the bound physical object.

### angular_velocity

```text
angular_velocity 0 0 15
```

Sets angular velocity on the bound physical object.

### text

```text
text OpenGenesisLINK
```

Sets bounded floating object text.

Existing mutation rules still apply:

- object must exist
- object must be owned by the Script owner
- owner Modify permission is required
- Parcel build policy applies to motion/transform operations
- vectors must be finite

## New queries

### water_level

```text
water_level water
```

Result:

- `water.ready`
- `water.height`

### world_time

```text
world_time clock
```

Result:

- `clock.ready`
- `clock.unix_ms`

## Expanded object_info

```text
object_info obj
```

v3 additionally returns:

- `obj.velocity`
- `obj.angular_velocity`
- `obj.parent_entity`
- `obj.link_number`
- `obj.linkset_count`
- `obj.text`

The v2 fields remain available.

## Expanded region_info

Region information now also returns:

- `region.water_height`

## Crossing integration

When a linkset completes Object Crossing v2, Core uses the persisted source-to-destination entity map to migrate Script object bindings.

This means a Script can continue using the same Script ID and VM state after the object has committed to the destination Region.

The rebinding occurs after source removal acknowledgement, not after destination staging.

## Limits

The existing VM and queue limits remain in force:

- bounded instruction count
- bounded output actions
- bounded VM state
- bounded query fields/results
- queue TTL
- maximum delivery attempts

v3 is still a native OpenGenesisLINK Script API, not full LSL compatibility.
