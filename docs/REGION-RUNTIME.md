# Region Runtime

`RegionRuntime` owns the first active simulation loop in OpenGenesisLINK. Each configured Region is represented by a runtime with its own tick thread.

Current responsibilities:

- fixed-rate tick loop
- entity collection
- avatar flag/count
- native physics world stepping
- live metrics (ticks, simulation FPS, entity/avatar/body counts)

The runtime is intentionally small. Terrain, scripting, interest management and scene replication are not implemented yet.
