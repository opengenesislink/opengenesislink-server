# OpenGenesis Physics — Physics v2

OpenGenesisLINK 12.0.0-dev promotes the native C++ physics layer from the early integration kernel to Physics v2.

Physics v2 remains an OpenGenesisLINK-owned implementation. It does not embed or wrap an OpenSimulator physics engine.

## Body state

A physics body contains:

- position and linear velocity
- rotation and angular velocity
- accumulated force and torque
- mass
- collision radius
- restitution
- friction
- linear damping
- angular damping
- gravity scale
- buoyancy
- dynamic/static state
- character marker

Inputs are validated and bounded before entering the solver.

## Integration

Dynamic bodies use bounded time steps and integrate:

- configured gravity
- per-body gravity scale
- buoyancy-adjusted gravity
- accumulated force by inverse mass
- accumulated torque by inverse mass
- linear damping
- angular damping
- linear position
- Euler angular state

Force and torque accumulators are cleared after each solver step.

## Terrain contact

The solver samples the Region terrain height and resolves body penetration against the ground.

Ground contact applies:

- position correction
- restitution
- horizontal friction
- low-speed vertical settling
- collision-contact reporting

Region Runtime converts these contacts into:

- `land_collision_start`
- `land_collision`
- `land_collision_end`

## Body-to-body collision

Physics v2 includes deterministic sphere broad/narrow pair testing for the current native primitive-body abstraction.

For overlapping bodies the solver performs:

- penetration correction
- normal impulse
- restitution
- friction impulse
- contact point/normal reporting

Region Runtime converts contacts into:

- `collision_start`
- `collision`
- `collision_end`

The event text contains the peer entity identifier and, when available, contact impulse metadata.

This collision model is intentionally a foundation for later shape-aware narrowphase. It does not claim mesh, convex-hull or triangle-mesh collision fidelity.

## Forces and impulses

Physics v2 exposes:

- continuous force
- linear impulse
- continuous torque
- angular impulse

These operations are available through Region Runtime, authenticated Scene Physics v2 and the Script-to-World action pipeline.

## Material and motion controls

Runtime-controllable body state includes:

- mass
- restitution
- friction
- buoyancy
- linear/angular velocity
- linear/angular damping
- gravity scale

The first public Scene Physics v2 contract exposes material, buoyancy, force, impulse, angular impulse and torque.

## Constraints

Physics v2 introduces bounded distance constraints between two physical bodies.

A distance constraint defines:

- body A
- body B
- rest length
- stiffness

The solver applies iterative positional correction. Constraint IDs can be removed explicitly, and constraints are automatically discarded when a referenced body is deleted.

This is the first native constraint type. Hinges, sliders, six-degree-of-freedom joints and articulated ragdolls remain future work.

## Linksets

Object Runtime v2 keeps one physical body on a linked root.

When the root moves or rotates, child parts are transformed as rigid offsets relative to the root. This fixes the earlier v1 limitation where only root translation propagated correctly.

Linked children do not own independent bodies in this contract.

## Avatar bodies

Avatars continue to own native PhysicsWorld bodies and participate in Physics v2 contacts.

12.0 improves the common collision foundation used by avatars and objects, but it does not claim a final character controller with stairs, slope limits, crouching, jumping policies or capsule-vs-mesh narrowphase.

## Scene Physics v2

Authenticated Scene clients can submit `entity_physics` operations for objects they are allowed to modify.

Supported operations:

- `force`
- `impulse`
- `angular_impulse`
- `torque`
- `buoyancy`
- `material`
- `constraint`
- `constraint_remove`

Scene snapshots append:

- mass
- restitution
- friction
- buoyancy

Existing snapshot fields retain their previous indices.

## Scripting integration

Native OGL gains:

- `world.force`
- `world.impulse`
- `world.angular_impulse`
- `world.torque`
- `world.buoyancy`
- `world.material`

LSL compatibility gains partial executable semantics for:

- `llApplyImpulse`
- `llApplyRotationalImpulse`
- `llSetForce`
- `llSetTorque`
- `llSetBuoyancy`

The current LSL mappings deliberately support a narrower subset than complete Second Life semantics. In particular, the implemented vector-force calls require supported constant vector forms and the currently documented world-space mode.

## Persistence and crossing

Scene Persistence v6 preserves the physical root state required by Physics v2:

- linear velocity
- angular velocity
- mass
- restitution
- friction
- linear damping
- angular damping
- gravity scale
- buoyancy

The loader remains compatible with earlier Scene object record layouts.

Object Crossing/linkset snapshots carry the same Physics v2 state, so a physical object does not silently revert to default material settings after a Region transfer.

## Current boundaries

Physics v2 does not yet claim:

- production-grade mesh/convex collision shapes
- continuous collision detection
- final avatar character controller
- full vehicle parameter compatibility
- articulated joints beyond distance constraints
- ragdolls
- fluid-volume simulation
- distributed cross-Region contact solving
- deterministic lockstep simulation across heterogeneous CPUs

Those are later roadmap items and must not be presented as implemented 12.0 features.
