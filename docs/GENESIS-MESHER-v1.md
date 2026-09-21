# OpenGenesisLINK GenesisMesher v1

## Purpose

GenesisMesher is the native OpenGenesisLINK geometry-to-mesh layer.

It follows the architectural idea that complex virtual-world geometry should be converted into an explicit, validated mesh before a physics backend consumes it. OpenSimulator has a comparable separation through its meshing layer and Meshmerizer, but GenesisMesher is an independent OpenGenesisLINK implementation written for the C++23 server architecture. No OpenSimulator mesher code is embedded or wrapped.

GenesisMesher is intended to serve several consumers:

- native OpenGenesis Physics collision preparation
- server-side geometry validation
- object and asset import pipelines
- future Viewer/Scene geometry metadata
- map/Atlas geometry processing where server-side low-detail geometry is useful
- offline tooling and diagnostics

## 17.0 v1 implementation

The first implementation is deterministic and procedural.

Supported native primitive inputs:

- box
- sphere / ellipsoid
- cylinder
- capsule

Output:

- indexed triangle mesh
- per-vertex position
- per-vertex normal
- optional UV coordinates
- axis-aligned bounds
- deterministic cache key
- validated index and triangle topology

Controls:

- radial segment count
- vertical segment count
- maximum triangle budget
- UV generation toggle

The mesher rejects non-finite or non-positive dimensions and removes degenerate triangles before validation.

## Collision representation policy

GenesisMesher deliberately does not make the physics solver depend on one collision representation.

The v1 API can recommend one of:

- `primitive_proxy`
- `convex_hull`
- `triangle_mesh`

Current policy:

- native box, sphere and capsule shapes prefer the native primitive collider
- dynamic non-native/complex geometry prefers a convex representation
- bounded static complex geometry may use a triangle mesh

This is a policy result, not a claim that Physics v3 already implements triangle-mesh narrowphase.

## Architecture

```text
Object / Asset Geometry
        |
        v
  Geometry Descriptor
        |
        v
  GenesisMesher v1
        |
        +--> validated render/geometry mesh
        |
        +--> bounds + cache key
        |
        +--> collision representation policy
                    |
                    v
          Physics Shape Builder
                    |
                    +--> primitive proxy
                    +--> convex hull
                    +--> triangle mesh   [future narrowphase]
```

The separation is intentional. Meshing, collision-shape construction and collision solving are different responsibilities.

## Cache design

The v1 cache key is deterministic and includes:

- mesher contract version
- primitive type
- object dimensions
- radial segments
- vertical segments
- triangle budget
- UV mode

A later server cache can use this key for memory and disk entries without changing the mesh-generation contract.

Planned cache layers:

1. per-Region hot memory cache
2. optional shared World-node memory cache
3. disk cache for expensive imported/sculpted geometry
4. asset-hash keyed invalidation

Cache entries must be bounded by memory, triangle count and age.

## Future geometry inputs

Planned GenesisMesher inputs include:

- OpenGenesis parametric prim profiles
- path/profile extrusion
- hollow/cut/twist/taper/shear parameters
- imported mesh assets
- convex decomposition input
- sculpt-map compatibility input for legacy OpenSim/Second Life assets
- compound linkset collision geometry
- simplified LOD generation

Legacy compatibility input does not imply that OpenGenesisLINK will reproduce OpenSimulator internals. Compatibility data will be translated into the OpenGenesis geometry model first.

## Physics integration roadmap

Phase M1 — completed foundation:

- deterministic primitive triangulation
- mesh validation
- bounds
- triangle budgets
- collision representation policy

Phase M2:

- `PhysicsShapeBuilder` interface
- immutable mesh handles
- static triangle-mesh collider data
- dynamic convex-hull builder
- cache ownership and lifecycle

Phase M3:

- BVH/AABB acceleration structure
- raycast against triangle meshes
- sphere/capsule versus triangle mesh
- box/convex versus triangle mesh
- contact manifold generation

Phase M4:

- convex decomposition
- compound linkset collision shape
- imported mesh/sculpt collision data
- persistence and Region crossing of shape references

Phase M5:

- background mesh build queue
- disk cache
- metrics, diagnostics and abuse limits
- deterministic LOD/collision simplification

## Safety and resource limits

Meshing is server-controlled. Asset data is untrusted input.

Production requirements include:

- maximum source bytes
- maximum vertices and triangles
- maximum material/submesh count
- recursion/decomposition limits
- build-time budget
- bounded worker queue
- cache memory ceiling
- finite-coordinate checks
- invalid/degenerate topology rejection
- protection against decompression bombs and malformed assets

## Non-goals of v1

GenesisMesher v1 does not yet claim:

- imported Collada/glTF/OBJ parsing
- sculpt-map decoding
- convex decomposition
- triangle-mesh collision solving
- automatic LOD simplification
- disk cache
- asynchronous worker pool
- Viewer-side rendering integration
- exact Second Life/OpenSimulator prim meshing parity

Those are explicit later roadmap items.
