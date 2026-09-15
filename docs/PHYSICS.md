# OpenGenesis Physics

The server contains its own native C++ physics foundation.

Current `0.3.0-dev` capabilities:

- rigid-body state
- gravity
- semi-implicit velocity/position integration
- configurable restitution
- body position and velocity mutation
- terrain-aware ground contact through a height sampler
- per-body collision radius used for ground contact

This is still an early solver kernel. Body-vs-body collision detection, broadphase/narrowphase, constraints, character controller, vehicles, water, buoyancy, ships and cross-region physics are not yet part of this implementation.
