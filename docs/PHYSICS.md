# OpenGenesis Physics — Foundation

The 0.2.0-dev tree contains the first native OpenGenesis Physics implementation rather than a third-party physics wrapper.

Current kernel features:

- rigid-body state
- configurable gravity
- semi-implicit Euler integration
- static ground-plane contact
- restitution
- thread-safe body creation/removal/query

This is an engineering foundation, not yet a production solver. Constraints, shapes, broadphase/narrowphase collision, vehicles, buoyancy, water and cross-region physics will be implemented on top of this subsystem.
