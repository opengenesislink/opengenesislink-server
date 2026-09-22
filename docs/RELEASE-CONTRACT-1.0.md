# OpenGenesisLINK Server 1.0.0-alpha.1 — Release Contract

## Status

**1.0.0-alpha.1** is the first versioned OpenGenesisLINK Server release intended to be consumed by the separate OpenGenesisLINK Viewer and OpenGenesisLINK Atlas projects.

It is an **alpha contract release**, not the final production-stable 1.0.0 release. The server is sufficiently coherent for first-party client development, integration testing and protocol evolution behind explicit version/capability checks.

The last large development milestone before this release line was 17.0.0-dev.

## Frozen alpha contracts

The following contract identifiers are the canonical integration baseline for Viewer and Atlas development:

- Release discovery: `ogl-release-v1`
- Core Web API: `api_version = 1`
- Viewer bootstrap: `ogl-viewer-bootstrap-v1`
- Scene application contract: `scene-v2`
- OGL1 framed Scene transport: `protocol = 1`
- Atlas API: `ogl-atlas-v1`
- Native federation: `OGL-FED/2`
- Voice capability contract: `ogl-voice-cap-v0`

Machine-readable discovery:

```text
GET /v1
GET /v1/release
```

Clients MUST feature-detect capabilities instead of assuming every optional subsystem is present.

## Compatibility rules for the alpha line

For the released contracts above:

1. Existing fields and message meanings are not removed or silently repurposed within the same release revision.
2. New JSON fields may be added. Clients must ignore unknown fields.
3. New capabilities may be added. Clients must negotiate/feature-detect them.
4. A breaking alpha contract change requires a new contract/release revision rather than silently changing the existing identifier.
5. Server-authoritative identity, permissions, Region admission and simulation state remain authoritative even if a client caches local state.
6. Persistence internals are not a client contract.

## Release discovery payload

`GET /v1/release` returns:

- server semantic version
- release channel
- API version
- Viewer contract id
- Scene contract id
- Atlas contract id
- Federation contract id
- Voice capability contract id
- compatibility expectations
- primary integration endpoints

Viewer and Atlas should check this endpoint before enabling contract-specific functionality.

## Viewer baseline

The Viewer can begin implementation against:

```text
POST /v1/auth/login
GET  /v1/auth/me
POST /v1/viewer/bootstrap
POST /v1/viewer/session
POST /v1/viewer/teleport
POST /v1/viewer/handoff
POST /v1/viewer/handoff/reserve
POST /v1/viewer/handoff/complete
POST /v1/viewer/handoff/rollback
```

World entry then uses the Scene endpoint and signed Scene Ticket returned by Core.

Canonical Viewer handover:

`docs/VIEWER-HANDOVER-1.0.0-alpha.1.md`

## Atlas baseline

The Atlas can begin implementation against the public read contract:

```text
GET /v1/atlas/bootstrap
GET /v1/atlas/regions
GET /v1/atlas/regions/{region-id}
```

The contract exposes Grid coordinates, Region state, Region runtime counters, neighbors and per-Region Parcel metadata without exposing individual Presence locations.

Canonical Atlas handover:

`docs/ATLAS-HANDOVER-1.0.0-alpha.1.md`

## Server subsystems available at this baseline

The release includes working foundations for:

- identity and bearer sessions
- Viewer Scene Tickets and capability checks
- World/Region registry
- Region Runtime and persistence
- Scene snapshot/delta synchronization
- Avatar Appearance and Inventory bootstrap
- Assets and permissions
- Parcels, Estates, Groups and Social services
- Economy and Marketplace
- native OGL-FED/2 federation
- Hypergrid compatibility adapters
- ScriptEngine with native OGL and partial LSL compatibility
- Physics v3 primitive collision/raycast/character foundations
- GenesisMesher v1 procedural meshing and bounded cache
- SQLite, PostgreSQL and MariaDB storage
- Linux x86_64, Linux ARM64/aarch64 and Windows x86_64 builds

## Deliberately deferred from the first alpha release

The following are not blockers for starting Viewer/Atlas development:

- complete 523-function LSL semantic parity
- production vehicle physics
- final character-controller prediction
- static arbitrary triangle-mesh narrowphase/BVH
- convex decomposition and imported mesh physics
- final mesh/material/animation streaming pipeline
- final QUIC/UDP Scene transport
- Viewer application implementation
- Atlas application implementation
- hosted Voice service implementation

These remain roadmap items and must arrive through explicit capability/contract extensions.

## Packaging

CMake install targets remain canonical. CPack is configured to produce:

- ZIP on Windows
- TGZ on Unix-like systems
- DEB on Debian-family build environments

The semantic release label is read from the repository `VERSION` file.

## Release acceptance gate

A 1.0.0-alpha.1 merge is accepted only when the final PR head is green for the existing project matrix:

- Linux x86_64 warnings-as-errors + unit tests + integrated smoke suites
- Linux ARM64/aarch64 warnings-as-errors + unit tests + integrated smoke suites
- Windows x86_64/MSVC warnings-as-errors + unit tests
- SQLite relational integration
- PostgreSQL relational integration
- MariaDB relational integration

The integrated smoke must additionally verify `ogl-release-v1` and `ogl-atlas-v1`.
