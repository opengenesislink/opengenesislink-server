# OpenGenesisLINK Atlas — Server Handover for 1.0.0-alpha.1

## Purpose

This document is the canonical starting specification for the **separate OpenGenesisLINK Atlas project**.

Atlas is intended to become the global map/discovery layer for OpenGenesisLINK worlds. The long-term product concept is a map experience closer to a modern geographic search system than a traditional fixed virtual-world map: searchable Regions, destinations and categories across participating worlds, with visibility controlled by the authoritative services.

The 1.0.0-alpha.1 server release provides the first stable minimum contract required to begin that project.

Target server baseline:

```text
OpenGenesisLINK Server 1.0.0-alpha.1
release contract: ogl-release-v1
atlas contract: ogl-atlas-v1
native federation: OGL-FED/2
```

## First compatibility check

Call:

```text
GET /v1/release
```

Require:

```text
release_contract = ogl-release-v1
contracts.atlas = ogl-atlas-v1
```

Clients must ignore unknown JSON fields and feature-detect extensions.

## Atlas v1 endpoints

### Bootstrap

```text
GET /v1/atlas/bootstrap
```

Returns:

- `atlas_contract = ogl-atlas-v1`
- API version
- coordinate-system metadata
- current Region list
- canonical Atlas endpoint names
- Viewer teleport integration endpoint name

### Region list

```text
GET /v1/atlas/regions
```

Returns the current Grid Region set.

Each Region currently carries:

- id
- display name
- online/offline state
- World Node id
- grid X
- grid Y
- World Node generation
- simulation tick counter
- entity count
- Avatar count
- Physics body count
- Scene event sequence/count metadata
- terrain revision
- current simulation FPS

These runtime counters are informational. Atlas must not use them as authorization state.

### Region detail

```text
GET /v1/atlas/regions/{region-id}
```

Returns:

- Region metadata
- cardinal neighboring Regions
- Parcel metadata for that Region

A missing Region returns `404 region-not-found`.

## Coordinate model

Atlas v1 declares:

```text
region_size_meters = 256
grid_x_axis = east
grid_y_axis = north
```

The canonical top-level map placement is based on integer Region grid coordinates.

The Atlas renderer should keep:

- Grid/Region coordinates
- local Region coordinates
- screen/tile coordinates

as separate types. Do not mix them into one floating-point coordinate type.

A Region occupies one 256x256 local-meter cell at its `grid_x/grid_y` address for the current contract.

## Initial map rendering

The first Atlas implementation does not need a final satellite-style map tile service.

A valid first implementation can render:

- Region cells/footprints from grid coordinates
- Region name labels
- online/offline state
- occupancy count
- selected Region detail
- neighboring Regions
- Parcel rectangles inside the selected Region

Terrain imagery/heightmap tile rendering can be added as a later Atlas contract extension.

## Search for the first Atlas alpha

Server 1.0.0-alpha.1 does not yet expose a dedicated server-side full-text search/index API.

Therefore the first Atlas may search the fetched Region dataset client-side by:

- Region name
- Region id

Do not claim global business/category search until a server/Atlas indexing contract exists.

The future search model may add:

- destination/place records
- categories
- Parcel listings
- commercial/Marketplace metadata
- events
- maturity/safety filters
- federated multi-grid discovery

Those should be introduced as a versioned Atlas extension, not overloaded into the initial Region list.

## Privacy boundary

Atlas v1 intentionally exposes Region-level occupancy counts, not the exact location of individual users.

Do not build a person-tracking map from `/v1/presence`.

Future public Presence/discoverability must be opt-in/policy-controlled and use a dedicated Atlas contract.

## Viewer handoff

Atlas does not directly teleport an Avatar.

The Viewer remains responsible for authenticated travel using:

```text
POST /v1/viewer/teleport
```

Atlas should pass a destination descriptor to the Viewer containing at minimum:

- Region id
- optional local X
- optional local Y
- optional local Z

The cross-application deep-link/IPC URI belongs to the Viewer/Atlas projects and is not frozen by Server 1.0.0-alpha.1.

## Federation model

Long-term Atlas should be able to aggregate multiple participating OpenGenesisLINK Grids.

Native inter-grid trust and travel use OGL-FED/2.

For the first Atlas project, keep two concepts separate:

1. **Grid source** — a Core/Atlas API origin
2. **Region** — a Region inside that Grid

Never assume Region ids are globally unique without a Grid identity namespace.

A future global Atlas identity should therefore key Regions by something equivalent to:

```text
(grid_id, region_id)
```

rather than `region_id` alone.

## Atlas backend recommendation

Even if the first UI can consume one Grid directly, the project should be structured for a backend/index service.

Recommended separation:

```text
Atlas Web/Desktop UI
        |
        v
Atlas API / Index
├── Grid Registry
├── Region Index
├── Search Index
├── Category/Place Index
├── Visibility/Policy Filter
├── Tile/Preview Cache
└── Federation Crawlers/Refresh Jobs
        |
        v
OpenGenesisLINK Grid Servers
GET /v1/release
GET /v1/atlas/bootstrap
GET /v1/atlas/regions
GET /v1/atlas/regions/{id}
```

This prevents every browser/client from crawling every Grid directly once global discovery grows.

## Suggested Atlas data model

Start with explicit types similar to:

```text
Grid
- id
- base_url
- display_name
- release_contract
- atlas_contract
- last_seen
- trust/visibility state

Region
- grid_id
- region_id
- name
- grid_x
- grid_y
- state
- avatars
- terrain_revision
- sim_fps
- last_refresh

Parcel
- grid_id
- region_id
- parcel_id
- name
- local rectangle
- public_entry
- build policy metadata
```

Future records can add Place, Category, Event, Listing and MapTile without changing the identity model.

## Refresh strategy

For the first alpha:

- fetch `/v1/release` on Grid registration/startup
- reject/disable unsupported Atlas contracts
- fetch `/v1/atlas/regions` periodically
- refresh selected Region detail on demand
- use `terrain_revision` as a future map-preview invalidation hint
- back off when a Grid is offline
- preserve last-known data with a visibly stale/offline state

Do not hammer World Nodes directly. Atlas consumes Core/Atlas contracts.

## UI concept for the first Atlas

The first useful Atlas UI should include:

- pan/zoom map
- Region cell layout
- Region labels
- online/offline indication
- occupancy indication
- text search
- selectable Region panel
- neighbor navigation
- Parcel overlay/detail
- "Open in Viewer" action

Later versions can add:

- global multi-grid search
- clothing/shop/service categories
- destination ranking/filtering
- events
- screenshots/map tiles
- route-like navigation between destinations
- Marketplace discovery
- public place registration
- policy/maturity filters

## Security and abuse controls

Treat all remote Grid metadata as untrusted input.

Atlas must:

- bound response sizes
- validate identifiers and coordinates
- escape all displayed text
- use HTTPS for Internet-facing Grid sources
- rate-limit refreshes
- isolate failing/untrusted Grid sources
- never execute content supplied as Region/Parcel metadata
- avoid exposing private user Presence

The server remains authoritative for travel/admission; Atlas is discovery/navigation only.

## Recommended implementation order

1. release/contract discovery
2. single-Grid Atlas bootstrap
3. Region map model
4. pan/zoom Region renderer
5. Region list refresh
6. Region detail + neighbors
7. Parcel overlay
8. local Region search
9. Viewer destination bridge
10. persisted Grid registry
11. Atlas backend/index
12. multi-Grid namespaced Regions
13. OGL-FED-aware Grid identity
14. destination/place/category records
15. map preview/tile service
16. global search/filtering
17. events/Marketplace discovery

## Definition of the first Atlas alpha

The first Atlas alpha is successful when it can:

- verify an OpenGenesisLINK Server contract
- load all Regions from one Grid
- place them correctly by grid coordinates
- show Region state and occupancy
- search Region names
- open Region details
- show neighboring Regions
- render Parcel boundaries for a selected Region
- hand a destination to the Viewer

That is enough to start Atlas development now without waiting for later Physics, Script or full content-pipeline milestones.

## Server documents to keep beside this handover

- `docs/RELEASE-CONTRACT-1.0.md`
- `docs/WEB-API-v1.md`
- `docs/OGL-FED-v2.md`
- `docs/REGION-RUNTIME.md`
- `docs/WORLD-OPERATIONS-v1.md`
- `docs/VIEWER-HANDOVER-1.0.0-alpha.1.md`
