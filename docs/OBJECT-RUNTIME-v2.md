# OpenGenesisLINK Object Runtime v2

OpenGenesisLINK 12.0.0-dev extends Object Runtime v1 with Physics v2 integration while retaining the existing root/child topology and transactional Object Crossing model.

## Topology

A linkset still consists of:

- one root object with `parent_entity_id = 0` and link number 1
- zero or more child objects referencing the root
- a maximum of 64 transferred members in the current crossing contract

Nested linkset roots remain unsupported.

## Rigid root transform

Object Runtime v2 propagates both root translation and root rotation.

For every child the runtime:

1. derives the child offset in the previous root-local frame,
2. transforms that offset into the new root frame,
3. moves the child around the root,
4. applies the same Euler rotation delta to the child orientation.

This is a major correction over Object Runtime v1, which only guaranteed root translation propagation.

## Physical ownership

The root owns the PhysicsWorld body for a linked object graph.

A linked child:

- does not keep an independent physical body,
- follows the root transform,
- keeps its object identity, permissions, link number and floating text.

Unlinking returns the child to standalone object topology. It does not automatically make the child physical.

## Physics v2 state

Physical roots can carry:

- linear velocity
- angular velocity
- mass
- restitution
- friction
- linear damping
- angular damping
- gravity scale
- buoyancy

The runtime exposes force, impulse, angular impulse and torque operations.

## Collision events

Physical roots and standalone objects participate in native Physics v2 collision contacts.

Region Runtime emits start/stay/end Scene events for:

- object/avatar body contacts
- terrain contacts

The Scene event stream therefore provides the first server-owned collision context for later automatic script-event dispatch.

## Distance constraints

Two independent physical entities can be connected through a distance constraint with a rest length and stiffness.

Constraints are runtime physics entities rather than linkset topology. Deleting a participating body removes its associated constraints.

## Scene protocol

Object Runtime v2 is exposed by Scene hello capability metadata as:

- `linkset-v2`
- `physics-v2`
- `collision-events-v1`

The authenticated Scene Physics operation uses message IDs 126/127.

## Persistence v6

Persistent Scene records now include Physics v2 material state while retaining loaders for legacy v1-v5 layouts.

## Object Crossing

Object Crossing snapshots carry Physics v2 root material and motion state. Linkset import preserves that state on the destination physical root before the source is removed.

This keeps the existing import-ACK-before-source-removal transaction semantics.

## Non-claims

Object Runtime v2 is not yet a full Second Life/OpenSimulator prim implementation.

Not included in this milestone:

- arbitrary joint graphs inside linksets
- independent child-body collision shapes
- final mesh/material/render state
- object Inventory graphs
- avatar attachment object graphs
- final vehicle model
- distributed ownership consensus
