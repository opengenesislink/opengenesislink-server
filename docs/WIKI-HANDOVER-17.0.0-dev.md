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

The policy does not imply that triangle-mesh narrowphase is complete. That integration is a later 17.0 step.

## Concept addition

GenesisMesher is now a permanent architecture component between object/asset geometry and OpenGenesis Physics.

See:

- `docs/GENESIS-MESHER-v1.md`
- `docs/PROJECT-CONCEPT-ROADMAP.md`
- `docs/PHYSICS.md`

## Remaining 17.0 work

- Region Runtime shape/raycast/jump integration
- Scene Physics v3 operations
- persistence/crossing shape state
- PhysicsShapeBuilder bridge
- first static mesh acceleration data
- World/Script integration for raycast and shape APIs
- process smoke expansion
- full cross-platform CI acceptance
