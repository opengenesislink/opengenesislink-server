# OpenGenesisLINK Project Concept & Roadmap

## Project direction

OpenGenesisLINK is an independent open-source virtual-world platform implemented primarily in C++23 under MPL 2.0.

The platform owns its native runtime, networking, federation, scripting, physics and geometry stack. OpenSimulator/Second Life interoperability is isolated behind explicit compatibility adapters and is not the architectural core.

Server targets:

- Linux x86_64
- Linux ARM64/aarch64, including Raspberry Pi 5 class systems
- Windows x86_64 / Windows Server

Relational storage targets:

- SQLite
- PostgreSQL
- MariaDB

Separate projects:

- OpenGenesisLINK Viewer
- OpenGenesisLINK Atlas
- OpenGenesisLINK Voice service

## Architecture

```text
Core Services
 identity / auth / inventory / assets / social / land / economy
            |
            v
World Node / Region Runtime
 scene / avatars / objects / terrain / scripts / crossing
      |                       |
      v                       v
GenesisMesher             ScriptEngine
geometry/collision prep   OGL + LSL compatibility
      |
      v
OpenGenesis Physics
      |
      v
Scene Protocol / Viewer

Core Atlas API ---------> OpenGenesisLINK Atlas
OGL-FED/2 --------------> other OpenGenesisLINK Grids
Hypergrid Adapter ------> legacy OpenSimulator compatibility
```

## Accepted development foundation

### Through 16.0.0-dev

Accepted foundations include:

- Core and World Node architecture
- persistent Region runtime and terrain
- native Scene protocol
- identity/session/auth
- Viewer bootstrap
- Inventory/Assets/Appearance
- Social/Groups/Notifications
- Parcels/Estates/Landmarks
- Economy/Marketplace
- moderation/audit/admin roles
- OGL-FED/2
- Hypergrid compatibility services
- Region and Object Crossing
- ScriptEngine with native OGL and partial LSL compatibility

### 17.0.0-dev — accepted

Physics v3 & GenesisMesher v1 added:

- sphere, scale-aware box and capsule shapes
- primitive and terrain raycasts
- capsule character grounding/jump foundation
- adaptive bounded substeps
- spring constraints
- deterministic procedural box/sphere/cylinder/capsule meshing
- validation, bounds, triangle budgets and cache identities
- bounded thread-safe GenesisMesher cache
- PhysicsShapeBuilder collision-plan bridge
- Region Runtime Physics v3 APIs
- Scene Physics v3 operations
- Scene Persistence v7 collision-shape state
- Object Crossing shape-state transport

## First release line

### 1.0.0-alpha.1 — first Viewer/Atlas development release

The old internal development milestone numbering ends at 17.0.0-dev. The public integration line begins at **1.0.0-alpha.1**.

Purpose:

- provide a coherent versioned server baseline
- freeze the first named Viewer/Scene/Atlas alpha contracts
- allow Viewer and Atlas to be developed as separate projects immediately
- continue deeper simulator work through additive/versioned capability extensions

Canonical contracts:

- `ogl-release-v1`
- Core `api_version=1`
- `ogl-viewer-bootstrap-v1`
- `scene-v2`
- OGL1 Scene transport protocol 1
- `ogl-atlas-v1`
- `OGL-FED/2`
- `ogl-voice-cap-v0`

New first-release work:

- machine-readable `GET /v1/release`
- dedicated Atlas v1 bootstrap/list/detail API
- alpha compatibility policy
- CPack ZIP/TGZ/DEB packaging foundation
- release/Atlas smoke coverage
- canonical Viewer project handover
- canonical Atlas project handover

## Viewer project baseline

Viewer development can start from the separately distributed 1.0.0-alpha.1 Viewer handover.

The first Viewer does not wait for complete LSL, vehicles or final arbitrary mesh physics. Its initial objective is login, Scene entry, rendering, Avatar control/reconciliation, asset/inventory consumption, social/chat and Region travel.

## Atlas project baseline

Atlas development can start from the separately distributed 1.0.0-alpha.1 Atlas handover.

Atlas v1 begins with Region discovery, map placement, Region runtime state, neighbors, Parcel overlays and Viewer destination handoff. Global place/category indexing remains an Atlas extension.

## Post-alpha server roadmap

### Advanced geometry and Physics

- static triangle-mesh BVH
- triangle-mesh raycasts
- sphere/capsule/convex vs static mesh contacts
- contact manifolds
- convex hull generation
- convex decomposition
- compound linkset colliders
- stairs/slopes Character improvements
- vehicle controller
- hinges/sliders/6-DOF constraints
- continuous collision improvements

### Content pipeline

- imported mesh validation
- internal immutable mesh-asset representation
- materials
- textures
- animation assets
- LOD/simplification
- legacy sculpt compatibility
- server-side conversion/cache
- permissions/export policy

### Scripting

- `link_message` / `llMessageLinked`
- sensor/no_sensor
- attachments
- permissions lifecycle
- full detected-event context
- HTTP/DataServer async model
- broader LSL semantic coverage
- OGL geometry/physics APIs
- script-facing raycast/shape actions after authoritative runtime contracts are final

Full LSL parity is not a prerequisite for Viewer/Atlas alpha development.

### Scene protocol

The first alpha freezes the named Scene v2 contract. Later capabilities may add:

- spatial interest management
- compressed/binary object deltas
- bandwidth prioritization
- final character prediction
- richer attachment replication
- mesh/material streaming
- animation protocol
- camera/controller extensions
- optional future transport beyond current OGL1 framed TCP

Breaking changes require a new contract revision.

### Atlas expansion

- global Grid registry/index
- namespaced `(grid_id, region_id)` identities
- destination/place records
- categories
- visibility policies
- map previews/tiles
- events
- Marketplace discovery
- global search
- OGL-FED-aware multi-grid discovery

### Federation

- portable asset/object transfer extensions
- stricter trust-policy controls
- remote content grants
- distributed Region handoff where justified
- Atlas discovery metadata

### Operations and production hardening

- richer metrics/tracing
- backup/restore workflows
- rolling migration guidance
- TLS/key rotation guidance
- abuse controls and quotas
- load/soak tests
- crash/recovery tests
- packaging/install service integration
- production security review

## GenesisMesher roadmap

### M1 — complete

- deterministic primitive triangulation
- validation
- bounds
- triangle budgets
- collision representation policy

### M2 — partially complete

Complete:

- PhysicsShapeBuilder
- bounded thread-safe memory cache
- explicit solver-readiness plans

Remaining:

- immutable/shared mesh handles
- static triangle-mesh collider data
- dynamic convex-hull builder
- shared/disk cache ownership

### M3

- BVH/AABB acceleration
- mesh raycasts
- primitive-vs-triangle mesh contacts
- contact manifold generation

### M4

- convex decomposition
- compound linksets
- imported mesh/sculpt collision inputs
- persisted/crossed mesh-shape references

### M5

- background build queue
- disk cache
- diagnostics/metrics/abuse limits
- deterministic collision LOD/simplification

## Design rules

- server-authoritative state
- explicit capability checks
- bounded untrusted input
- portable C++23
- deterministic tests where practical
- no silent dependency on OpenSimulator internals
- compatibility adapters isolated from native protocols
- versioned persistence and wire contracts
- clients ignore unknown additive JSON fields
- breaking client-contract changes require new revisions
- Linux x86_64, ARM64 and Windows kept green
- SQLite, PostgreSQL and MariaDB integration kept green
