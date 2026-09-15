# Region Runtime

Each configured region owns a long-lived C++ `RegionRuntime` process object inside the World Node.

In `0.3.0-dev` the runtime provides:

- configurable simulation tick rate
- 256×256 heightfield Terrain at 1 metre cell size
- transform-based Scene entities
- object and avatar entity kinds
- native physics bodies linked to entities
- terrain-aware physics ground sampling
- create/update/delete operations
- avatar presence lifecycle
- local chat events
- bounded, monotonic Scene event journal
- snapshots and event polling through the Scene endpoint
- metrics for ticks, entities, avatars, physics bodies, Scene events and Terrain revision

A major runtime property is that Region Runtime threads remain active if the Core becomes temporarily unavailable. Core registration and leases are control-plane functions; the local simulation is not torn down just because the control plane reconnects.
