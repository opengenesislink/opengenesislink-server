# OpenGenesis Physics — Physics v3 development

OpenGenesisLINK 17.0.0-dev advances the native C++ physics layer from Physics v2 toward shape-aware Physics v3.

Physics remains an OpenGenesisLINK-owned implementation. It does not embed or wrap an OpenSimulator physics engine.

## Physics v2 baseline retained

The existing solver still provides:

- position and velocity integration
- angular state
- mass, restitution and friction
- damping
- gravity scale and buoyancy
- force, torque and impulses
- terrain contact
- body collision reporting
- distance constraints
- Region Runtime collision events

## Physics v3 core

17.0 adds native collision-shape state:

- sphere
- axis-aligned box
- vertical capsule

Body state now includes:

- half extents
- capsule half height
- collision shape
- grounded state
- character-controller parameters

Shape-aware narrowphase currently covers combinations of sphere, box and capsule.

Rotated OBB, convex hull and triangle-mesh narrowphase are not yet claimed.

## Adaptive substeps

Physics v3 bounds large frame deltas and divides integration into smaller substeps.

This improves stability for:

- faster bodies
- spring constraints
- character contact
- shape-aware collision resolution

It is not full continuous collision detection.

## Raycasts

Physics v3 introduces bounded raycasts against:

- supported primitive bodies
- Region terrain

Raycast results include:

- body identifier
- point
- normal
- distance
- terrain/ground marker

Region Runtime integration is a separate 17.0 step.

## Character controller foundation

Capsule bodies can act as characters.

The v3 foundation adds:

- grounded state
- maximum slope parameter
- step-height parameter
- jump speed
- grounded jump

This is not yet the final production Avatar controller.

## Constraints

Physics v3 retains distance constraints and adds damped spring constraints.

A spring defines:

- body A
- body B
- rest length
- stiffness
- damping

## GenesisMesher relationship

17.0 adds GenesisMesher as a separate geometry layer.

GenesisMesher can produce validated indexed triangle meshes for native procedural geometry. It also recommends whether physics should use:

- a native primitive proxy
- a convex hull
- a triangle mesh

Physics v3 does not yet claim triangle-mesh narrowphase. A dedicated PhysicsShapeBuilder is now present so the solver never has to parse arbitrary asset data itself. It maps supported primitive geometry to native solver shapes and marks convex/triangle plans as pending rather than silently degrading them.

See `docs/GENESIS-MESHER-v1.md`.

## 17.0 integration status

Completed in the current 17.0 branch:

- Region Runtime shape state
- scale-aware object collider mapping
- Scene Physics v3 actions
- persistence and crossing of shape state
- GenesisMesher bounded memory cache
- PhysicsShapeBuilder collision plans

Next:

- static mesh BVH foundation
- mesh raycast
- primitive/capsule versus static mesh contacts
- convex-hull builder for dynamic complex objects

## Explicit non-claims

Current 17.0 development does not yet claim:

- rotated OBB collision
- convex decomposition
- triangle-mesh collision resolution
- continuous collision detection
- production vehicle physics
- final stair/step character controller
- ragdolls
- fluid simulation
- distributed cross-Region contact solving
