# Wiki Handover — OpenGenesisLINK Server 17.0.0-dev

## Status

17.0.0-dev is currently in development on:

`dev/17.0.0-physics-meshing-v1`

Milestone theme:

**Physics v3 & GenesisMesher v1**

This milestone starts from the accepted 16.0.0-dev main line. The old PR #21 is not merged directly because it diverged from the 16.0 accepted Script branch. Reusable Physics-v3 core work is being ported selectively instead.

## Implemented so far

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

## Remaining 17.0 work

- first static mesh acceleration data / BVH
- convex-hull generation
- actual triangle-mesh narrowphase
- optional OGL scripting exposure for shape/raycast
- process smoke expansion
- full cross-platform CI acceptance
