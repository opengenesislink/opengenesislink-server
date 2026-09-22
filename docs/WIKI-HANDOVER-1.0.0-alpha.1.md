# OpenGenesisLINK Server 1.0.0-alpha.1 — Wiki / Release Handover

## Status

**Accepted first client-development release.**

- Release: `1.0.0-alpha.1`
- PR: #24
- Final tested PR head: `bdbbb31a805782aeda8e53e3e274cab7141908a1`
- Squash merge to `main`: `af40e4cd58c6fb98dbce6c5e25c23aa09b4fb730`
- Release contract: `ogl-release-v1`
- Viewer contract: `ogl-viewer-bootstrap-v1`
- Scene contract: `scene-v2`
- Atlas contract: `ogl-atlas-v1`
- Federation contract: `OGL-FED/2`

This is the first OpenGenesisLINK Server release baseline intended for parallel development of the separate Viewer and Atlas projects. It is an alpha integration release, not the final production-stable 1.0.0.

The detailed Viewer and Atlas project handovers are intentionally distributed outside this server repository.

## What changed for the first release

### Release discovery

New machine-readable endpoint:

```text
GET /v1/release
```

It identifies the server version, release channel, Viewer/Scene/Atlas/Federation/Voice contracts and compatibility expectations.

### Atlas v1

New public read contract:

```text
GET /v1/atlas/bootstrap
GET /v1/atlas/regions
GET /v1/atlas/regions/{region-id}
```

Atlas v1 exposes the minimum server-authoritative Region data required to begin the separate Atlas project:

- Region id/name
- online/offline state
- Grid X/Y position
- World Node association
- runtime counters
- Avatar occupancy count
- terrain revision
- neighboring Regions
- Parcel metadata on Region detail

Exact individual Presence positions are deliberately not part of the public Atlas contract.

### Viewer / Scene baseline

The existing Viewer bootstrap and Scene v2 work is now promoted to the first released alpha integration baseline:

- `ogl-viewer-bootstrap-v1`
- `scene-v2`
- OGL1 framed Scene transport protocol 1
- Scene snapshot/delta recovery
- Avatar reconciliation
- Region and Parcel metadata
- signed Scene Tickets and capability checks

Additive capabilities and unknown JSON fields must be tolerated by clients. Breaking changes require a new contract revision.

### Packaging foundation

CMake/CPack now has release packaging foundations for:

- Windows ZIP
- Unix TGZ
- Debian DEB

The semantic release string is read from the repository `VERSION` file and compiled into the server API instead of being reduced to the numeric CMake project version.

## Acceptance matrix

Final PR head `bdbbb31a805782aeda8e53e3e274cab7141908a1` passed:

### Linux x86_64

- warnings-as-errors configure: PASS
- build: PASS
- unit tests: PASS
- integrated world/social/handoff smoke: PASS
- Script World query and ACK smoke: PASS
- OGL and LSL ScriptEngine smoke: PASS
- Crossing v3 reserve/rollback smoke: PASS
- Object Crossing end-to-end smoke: PASS
- Viewer bootstrap / Scene v2 smoke: PASS
- OGL-FED v2 remote services smoke: PASS
- Platform Services smoke: PASS
- release discovery / Atlas v1 assertions inside integrated smoke: PASS

### Linux ARM64/aarch64

- warnings-as-errors configure: PASS
- build: PASS
- unit tests: PASS
- integrated world/social/handoff smoke: PASS
- Script World query and ACK smoke: PASS
- OGL and LSL ScriptEngine smoke: PASS
- Crossing v3 reserve/rollback smoke: PASS
- Object Crossing end-to-end smoke: PASS
- Viewer bootstrap / Scene v2 smoke: PASS
- OGL-FED v2 remote services smoke: PASS
- Platform Services smoke: PASS
- release discovery / Atlas v1 assertions inside integrated smoke: PASS

### Windows x86_64 / MSVC

- warnings-as-errors configure: PASS
- Core/World/tests build: PASS
- unit tests: PASS

### Relational backends

- SQLite integration: PASS
- PostgreSQL integration: PASS
- MariaDB integration: PASS

## Server baseline available to Viewer

The separate Viewer can now build against a versioned server contract covering:

- authentication/session lifecycle
- aggregated Viewer bootstrap
- Scene entry and signed Scene Tickets
- snapshot/delta synchronization
- Avatar reconciliation
- terrain/Region/Parcel metadata
- objects and permissions
- Appearance, Inventory and Assets
- local chat plus Core Social/Groups
- Economy/Marketplace
- teleport and Region handoff
- Atlas destination integration point
- Voice provider abstraction

## Server baseline available to Atlas

The separate Atlas can now build against:

- release compatibility discovery
- Region map placement by Grid coordinates
- Region state/occupancy
- Region detail
- Region neighbors
- Parcel overlays
- Viewer teleport endpoint discovery

A later Atlas contract can add global place/category search, preview tiles, events, Marketplace discovery and multi-Grid indexing without blocking the first Atlas implementation.

## Deliberately deferred after alpha.1

These are post-alpha extensions and are not blockers for beginning Viewer/Atlas:

- complete LSL semantic compatibility
- arbitrary static triangle-mesh BVH/narrowphase
- convex decomposition and imported mesh Physics
- final mesh/material/animation content pipeline
- final low-latency Scene transport
- final Avatar prediction/controller
- vehicle Physics
- global Atlas place/category index
- hosted Voice service

## Next development tracks

From this release, development can split into three coordinated tracks:

1. **Server post-alpha** — advanced meshing/collision, content pipeline, ScriptEngine completeness and production hardening.
2. **Viewer** — start against the separately distributed Viewer handover.
3. **Atlas** — start against the separately distributed Atlas handover.

Server contract changes affecting either client must remain versioned and capability-discoverable.
