# Wiki Handover — OpenGenesisLINK Server 12.0.0-dev

## Milestone

OpenGenesisLINK Server 12.0.0-dev is the **World Runtime & Physics v2** milestone.

- repository: `opengenesislink/opengenesislink-server`
- pull request: #17
- final tested PR head: `5d4db8dd7a8340c286d716e793380689f5d0b9e5`
- squash merge: `414a65e8b01cf9aded64e13f183eea24df69e3c7`
- version: `12.0.0-dev`
- implementation: C++23
- license: MPL 2.0
- required CI targets: Linux x86_64, Linux ARM64/aarch64, Windows x86_64/MSVC
- required database integration: SQLite, PostgreSQL, MariaDB

12.0 deepens the simulation layer built on the 10.0 production foundation and the 11.0 Script Runtime Completion milestone.

## Native Physics v2

Physics v2 is an OpenGenesisLINK-owned runtime. It does not embed an OpenSimulator physics engine.

A body can now carry:

- position and linear velocity
- Euler rotation and angular velocity
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

The step is time-bounded and body iteration is deterministic by body ID.

## Contacts and collision solver

Physics v2 resolves the current primitive-body abstraction using sphere contacts.

Body-to-body resolution includes:

- overlap detection
- positional penetration correction
- normal impulse
- restitution
- friction impulse
- contact point and normal
- contact impulse metadata

Terrain collision uses the Region terrain sampler and produces ground contacts.

This is a native simulation foundation, not a claim of final mesh/triangle collision fidelity.

## Collision event lifecycle

Region Runtime converts Physics v2 contacts into Scene events.

Body contacts:

- `collision_start`
- `collision`
- `collision_end`

Terrain contacts:

- `land_collision_start`
- `land_collision`
- `land_collision_end`

The peer entity ID and contact impulse are exposed where applicable.

These events establish server-owned collision context for later automatic LSL/OGL event dispatch.

## Forces and material state

World Runtime exposes:

- force
- impulse
- angular impulse
- torque
- mass/restitution/friction
- buoyancy
- velocity/angular velocity

PhysicsWorld also owns linear/angular damping and gravity scale state.

## Constraints

12.0 introduces the first native constraint type: a bounded distance constraint.

A constraint contains:

- body A
- body B
- rest length
- stiffness

Constraints are removed automatically when a referenced body is deleted.

Hinge, slider, six-degree-of-freedom, ragdoll and articulated-joint systems remain future work.

## Object Runtime v2

Linksets retain the existing root/child topology.

A linked child has no independent physics body. The root owns physical motion.

12.0 fixes the important v1 limitation where only root translation propagated reliably. Child transforms now follow both root translation and root rotation by transforming their local offset through the old and new root frames.

Maximum transferred linkset size remains 64 members in the current crossing contract.

## Scene Physics v2

Authenticated Scene clients can mutate allowed objects through the new Physics v2 message path.

Supported actions:

- force
- impulse
- angular impulse
- torque
- buoyancy
- material
- distance constraint create
- constraint remove

Object modification capability and ownership/permission checks remain enforced.

Scene hello metadata advertises:

- `linkset-v2`
- `physics-v2`
- `collision-events-v1`

## Scripting

The shared ScriptEngine remains the only script runtime.

### Native OGL

The completed OGL-v2 language core remains 31/31.

12.0 adds six native Physics commands:

- `world.force`
- `world.impulse`
- `world.angular_impulse`
- `world.torque`
- `world.buoyancy`
- `world.material`

The current OGL catalog is therefore **37/37 implemented**.

### LSL compatibility

12.0 adds partial executable world-physics mappings for:

- `llApplyImpulse`
- `llApplyRotationalImpulse`
- `llSetForce`
- `llSetTorque`
- `llSetBuoyancy`

Current function matrix:

| Status | Count |
| --- | ---: |
| Total catalog | 523 |
| Implemented | 56 |
| Partial | 29 |
| Recognized | 413 |
| Unsupported | 25 |

Strict implementation: **10.71%**.

Executable including partial: **16.25%**.

The canonical command matrix is:

`docs/SCRIPT-COMMAND-STATUS-12.0.md`

12.0 does not claim full Second Life LSL semantics.

## Persistence v6

Scene Persistence v6 adds Physics v2 state for physical roots:

- linear velocity
- angular velocity
- mass
- restitution
- friction
- linear damping
- angular damping
- gravity scale
- buoyancy

The loader retains backward compatibility with older Scene record layouts.

## Object Crossing

Object Crossing v2 and linkset transfer preserve Physics v2 motion/material state.

The existing transaction remains:

1. source export
2. destination import
3. destination ACK
4. source removal
5. completion

Rollback/reconciliation behavior remains intact.

## Avatar physics

Avatars use native PhysicsWorld bodies and now participate in the common contact foundation.

12.0 is **not** the final avatar character controller. Remaining work includes production stair/slope handling, jump/crouch policy, final capsule/shape collision and viewer movement prediction/reconciliation.

## API / capability discovery

12.0 advertises World Runtime/Physics v2 capability metadata through the existing API discovery and Scene hello contracts.

Key concepts:

- `physics-v2`
- `linkset-v2`
- `collision-events-v1`
- Script Physics v2 actions
- Scene Persistence v6

## Verification

Acceptance requires the exact final PR head to pass:

- Linux x86_64 build with warnings-as-errors
- Linux ARM64/aarch64 build with warnings-as-errors
- Windows x86_64/MSVC build with warnings-as-errors
- unit tests
- integrated world/social/handoff smoke
- Script World query/ACK smoke
- OGL/LSL ScriptEngine Physics v2 smoke
- Crossing v3 smoke
- Object Crossing smoke
- SQLite integration
- PostgreSQL integration
- MariaDB integration

## Explicit non-claims

12.0 does not claim:

- production mesh/convex/triangle-mesh narrowphase
- continuous collision detection
- final avatar character controller
- complete vehicle implementation
- articulated constraints beyond distance constraints
- ragdolls
- fluid-volume simulation
- distributed cross-Region collision solving
- complete LSL semantic compatibility
- stable 1.0 API

## Acceptance result

All required gates passed on the final PR head `5d4db8dd7a8340c286d716e793380689f5d0b9e5`:

- Linux x86_64: PASS
- Linux ARM64/aarch64: PASS
- Windows x86_64/MSVC: PASS
- Unit tests: PASS
- integrated world/social/handoff smoke: PASS
- Script World query/ACK smoke: PASS
- OGL/LSL Physics ScriptEngine smoke: PASS
- Crossing v3 smoke: PASS
- Object Crossing smoke: PASS
- SQLite integration: PASS
- PostgreSQL integration: PASS
- MariaDB integration: PASS

Canonical 12.0 squash merge:

`414a65e8b01cf9aded64e13f183eea24df69e3c7`

## Roadmap after 12.0

The next major server milestone should focus on **13.0 — Viewer & Scene Protocol Completion**.

That block should formalize the native viewer-facing data plane: scene/entity streaming, movement reconciliation, appearance, attachments, assets/inventory delivery, parcel/region metadata, teleport flow and the contracts needed by the separate OpenGenesisLINK Viewer project.
