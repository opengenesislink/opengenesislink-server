# Wiki Handover — OpenGenesisLINK Server 17.0.0-dev

## Status

OpenGenesisLINK Server 17.0.0-dev is the accepted **Physics v3 & GenesisMesher v1** milestone.

- Pull request: #23
- Final tested PR head: `b7beed4ba4ee67c9c6ad9e850439c4179becda69`
- Squash merge: `2291aa5e55369cbf85f15c007183daf12e39b3e9`
- Canonical branch: `main`

The milestone starts from the accepted 16.0.0-dev main line. The older divergent PR #21 was not merged directly; reusable Physics-v3 core work was selectively ported onto the current main line so the accepted 16.0 Script work remained intact.

## Implemented

### Physics v3 core

- sphere collision shape
- axis-aligned box collision shape
- vertical capsule collision shape
- shape-aware primitive contacts
- primitive raycast
- terrain raycast
- capsule character foundation
- grounded state
- jump
- adaptive bounded physics substeps
- damped spring constraints
- collision-shape parser/name contract

### GenesisMesher v1

New files:

- `include/opengenesis/meshing/genesis_mesher.hpp`
- `src/meshing/genesis_mesher.cpp`
- `tests/meshing_tests.cpp`

Supported procedural geometry:

- box
- sphere / ellipsoid
- cylinder
- capsule

Implemented mesh controls:

- radial segments
- vertical segments
- triangle budget
- UV toggle
- bounded thread-safe in-memory cache
- cache hit/miss/eviction metrics

Implemented output and validation:

- indexed triangle mesh
- normals
- UVs
- bounds
- deterministic cache key
- degenerate-triangle removal
- finite/index validation

Collision-policy output:

- primitive proxy
- convex hull
- triangle mesh

The new PhysicsShapeBuilder turns that output into an explicit Physics collision plan. Native box/sphere/capsule plans are solver-ready. Cylinder/complex plans remain deliberately not solver-ready until the convex/triangle narrowphase exists.

The policy does not imply that triangle-mesh narrowphase is complete.

## Concept addition

GenesisMesher is now a permanent architecture component between object/asset geometry and OpenGenesis Physics.

See:

- `docs/GENESIS-MESHER-v1.md`
- `docs/PROJECT-CONCEPT-ROADMAP.md`
- `docs/PHYSICS.md`

## Additional 17.0 work completed

- Region Runtime scale-aware shape state
- Region Runtime raycast
- capsule Avatar controller configuration and jump
- spring constraints
- Scene Physics v3 actions
- Scene Persistence v7 collision-shape state
- Object Crossing collision-shape state
- PhysicsShapeBuilder bridge
- bounded GenesisMesher cache
- API capability discovery for Physics v3 and GenesisMesher

## CI hardening

- CMake project version is aligned to 17.0.0.
- the integrated smoke no longer hard-codes an old milestone version
- expected API version is derived from the repository `VERSION` file
- this prevents future milestone bumps from producing false-negative smoke failures

## Acceptance

The final tested PR head `b7beed4ba4ee67c9c6ad9e850439c4179becda69` passed:

- Linux x86_64 warnings-as-errors build
- Linux x86_64 unit tests
- Linux x86_64 integrated world/social/handoff smoke
- Linux x86_64 Script World query/ACK smoke
- Linux x86_64 OGL/LSL ScriptEngine smoke
- Linux x86_64 Crossing v3 smoke
- Linux x86_64 Object Crossing end-to-end smoke
- Linux x86_64 Viewer bootstrap / Scene v2 smoke
- Linux x86_64 OGL-FED v2 remote services smoke
- Linux x86_64 Platform Services smoke
- Linux ARM64/aarch64 with the same complete build/unit/smoke matrix
- Windows x86_64/MSVC warnings-as-errors build and unit tests
- SQLite integration
- PostgreSQL integration
- MariaDB integration

Canonical 17.0.0-dev squash merge: `2291aa5e55369cbf85f15c007183daf12e39b3e9`.

## Deferred beyond the accepted 17.0 milestone

These are explicit next-generation geometry/physics targets, not 17.0 completion blockers:

- static triangle-mesh acceleration structure / BVH
- convex-hull generation and convex decomposition
- actual triangle-mesh narrowphase
- imported mesh and sculpt compatibility inputs
- optional native OGL scripting exposure for advanced shape/raycast operations
- asynchronous/disk-backed meshing cache
