# Region Persistence v0

OpenGenesisLINK `0.4.0-dev` introduces the first persistent World Runtime state.

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
