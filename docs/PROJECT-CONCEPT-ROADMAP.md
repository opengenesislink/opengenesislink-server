# OpenGenesisLINK Project Concept & Roadmap

## Project direction

OpenGenesisLINK is an independent open-source virtual-world platform implemented primarily in C++23 under MPL 2.0.

The target is a complete server platform with its own runtime, networking, federation, scripting, physics and geometry stack. Compatibility with OpenSimulator/Second Life concepts is provided through explicit compatibility layers where useful; compatibility components must not become the architectural core.

Primary server targets:

- Linux x86_64
- Linux ARM64/aarch64, including Raspberry Pi 5 class systems
- Windows x86_64 / Windows Server

Primary relational storage targets:

- SQLite
- PostgreSQL
- MariaDB

Separate projects remain planned for:

- OpenGenesisLINK Viewer
- OpenGenesisLINK Atlas
- hosted/self-hosted OpenGenesisLINK Voice service

## Architectural layers

```text
Core Services
  identity / sessions / assets / inventory / economy / social
            |
            v
World Node / Region Runtime
  scene / objects / avatars / parcels / scripts
            |
      +-----+-----------------------+
      |                             |
      v                             v
GenesisMesher                  ScriptEngine
geometry preparation           OGL + LSL compatibility
      |
      v
OpenGenesis Physics
collision / raycast / constraints / character controller
      |
      v
Scene Protocol / Viewer
```

Federation is native through OGL-FED. Hypergrid remains a separate legacy compatibility gateway.

## GenesisMesher

GenesisMesher is now a first-class subsystem in the OpenGenesisLINK concept.

Its role is comparable in architecture to the meshing stage used by OpenSimulator before physics, but it is a new OpenGenesisLINK implementation.

Responsibilities:

- turn OpenGenesis geometry descriptions into validated indexed triangle meshes
- generate geometry for native procedural primitives
- prepare future imported/sculpted/parametric assets
- enforce triangle and resource budgets
- provide deterministic cache identities
- provide bounds and geometry metadata
- select or prepare collision representations for OpenGenesis Physics

The Mesher must remain independent from the physics solver. Physics consumes immutable collision geometry or primitive/convex proxies; it does not parse arbitrary asset formats itself.

Long-term GenesisMesher targets:

- classic prim/profile/path compatible geometry translation
- mesh asset ingestion
- legacy sculpt-map compatibility
- convex-hull generation
- convex decomposition
- static triangle-mesh collision data
- linkset compound geometry
- LOD/simplification
- bounded asynchronous build workers
- memory/disk caching
- diagnostics and metrics

## Current milestone

### 16.0.0-dev — accepted

Completed Script & LSL Event Expansion:

- automatic state lifecycle
- collision and land-collision Script events
- authenticated Scene touch pipeline
- first native `changed` sources
- 37/37 native OGL commands implemented
- expanded LSL function/event executable coverage

### 17.0.0-dev — in development

Theme: **Physics v3 & GenesisMesher v1**

Physics v3 core work being carried forward cleanly from the older experimental branch:

- sphere, box and capsule collision shapes
- scale-aware box shape data
- capsule character-controller foundation
- grounded state and jump
- adaptive bounded substeps
- primitive and terrain raycasts
- damped spring constraints

GenesisMesher v1:

- procedural box meshing
- procedural sphere/ellipsoid meshing
- procedural cylinder meshing
- procedural capsule meshing
- mesh validation
- degenerate-triangle removal
- bounds
- deterministic cache key
- triangle budgets
- collision representation policy
- bounded GenesisMesher in-memory cache with hit/miss/eviction metrics
- PhysicsShapeBuilder bridge from meshed geometry to Physics collision plans
- Region Runtime shape/raycast/spring/avatar-jump integration
- Scene Persistence v7 shape-state storage
- object-crossing shape-state transport
- authenticated Scene Physics v3 operations

Next 17.0 integration steps:

1. static triangle-mesh acceleration structure foundation
2. convex-hull generation for dynamic complex geometry
3. mesh raycast and primitive-vs-static-mesh narrowphase
4. script-facing OGL raycast/shape actions after server-authoritative World integration
5. process-smoke expansion for Scene Physics v3
6. cross-platform CI acceptance

## Roadmap after 17.0

### Geometry & Physics

- triangle-mesh BVH
- mesh raycast
- capsule/sphere vs mesh contacts
- convex hull and decomposition
- compound linkset colliders
- better character stairs/slopes
- vehicle controller
- hinges, sliders and 6-DOF constraints
- continuous-collision improvements for fast bodies

### Scripting

- `link_message` / `llMessageLinked`
- sensor/no_sensor
- attachments
- permissions lifecycle
- full detected-event context
- HTTP/DataServer async model
- broader LSL function semantics
- native OGL APIs for new geometry/physics capabilities

### Scene & Viewer protocol

- mesh/shape metadata
- asset streaming contracts
- object editing for advanced geometry
- stable capability discovery
- protocol versioning and migration policy

### Content pipeline

- imported mesh validation
- materials
- textures
- animation assets
- permissions/export policy
- server-side conversion and cache

### Federation

- OGL-FED service expansion
- portable asset/object transfer
- trust policies
- remote inventory/content grants
- distributed Region handoff where appropriate

### Operations

- metrics and tracing
- admin roles
- abuse controls
- backup/restore contracts
- rolling migration support
- production TLS/key rotation guidance

## Design rules

OpenGenesisLINK development should continue to follow these rules:

- server-authoritative state
- explicit capability checks
- bounded untrusted input
- portable C++23 implementation
- deterministic tests where practical
- no silent dependency on OpenSimulator internals
- compatibility adapters isolated from native protocols
- versioned persistence and wire contracts
- Linux x86_64, ARM64 and Windows kept green
- SQLite, PostgreSQL and MariaDB integration kept green
