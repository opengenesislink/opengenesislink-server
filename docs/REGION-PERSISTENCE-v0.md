# Region Persistence v0

OpenGenesisLINK `0.4.0-dev` introduced the first persistent World Runtime state.

Each region receives its own directory under the configured World Node storage root, for example:

```text
data/world/genesis-central/
  objects.db
  terrain.oglt
```

## Objects

`objects.db` stores persistent scene objects and their stable entity IDs, names, transforms and physical/non-physical state. Avatar presences are intentionally not persisted.

## Terrain

`terrain.oglt` is a small binary format containing:

- `OGLT` magic
- format version
- terrain dimensions
- cell size and base-height metadata
- terrain revision
- the full heightfield as IEEE-754 doubles

The file uses explicit little-endian integer/double serialization and is therefore not tied to compiler struct layout.

## Save behavior

The World Node saves changed state periodically and performs a forced save during a clean shutdown. Persistence continues while the Core is temporarily unavailable, because Region Runtime ownership remains with the World Node.

This is an early persistence format and may change before a stable release.


## Scene object persistence v4 (7.5.0-dev)

OpenGenesisLINK 7.5 writes Scene object records as v4.

Compared with the previous v3 object record, v4 appends:

- linear velocity X
- linear velocity Y
- linear velocity Z

Velocity is restored for physical objects after the object body is recreated.

The loader remains backward-readable for the historical 12-field, 13-field and 17-field Scene-object records. New v4 records use 20 fields.

The current persistence format still does not carry angular velocity, constraints, linkset graphs or vehicle-specific physics state.
