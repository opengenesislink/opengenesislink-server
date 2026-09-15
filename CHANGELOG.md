# Changelog

## 0.4.0-dev — 2026-09-15

Identity, persistence and browser/API milestone.

- added persistent Core identity store
- added PBKDF2-HMAC-SHA256 password hashing with random salts using OpenSSL
- added persistent hashed bearer-session store and cryptographically random session tokens
- added register, login, current-user and logout HTTP endpoints
- added browser-readable Core dashboard at `/`
- added API discovery at `/v1`
- added dedicated `/v1/worlds`, `/v1/regions`, `/v1/identity/stats` and `/v1/identity/users` endpoints
- expanded `/v1/status` with uptime, identity and session information
- added persistent per-region scene-object storage
- added binary terrain persistence and restart restore
- added Scene protocol terrain mutation (`TERRAIN_SET`)
- World Node persistence continues independently of Core connectivity
- expanded unit tests for Identity, Sessions and Region persistence
- expanded smoke test across Core restart and full World Node restart
- kept public repository limited to source, configuration and technical documentation

## 0.3.0-dev — 2026-09-15

Scene Runtime milestone.

- added 256×256 heightfield terrain runtime
- added scene entities and avatar presences
- added object create/update/delete, local chat, snapshots and event stream
- added first development Scene endpoint
- coupled native Physics ground sampling to terrain
- Region Runtime continues while Core is unavailable
- fixed OGL header byte layout and locked it with an interoperability test

## 0.2.0-dev — 2026-09-15

First larger runtime milestone.

- generation-based World Node reconnect sessions
- node leases and stale-node expiry foundation
- live Region Runtime and metrics
- native OpenGenesis Physics kernel
- HTTP health/status endpoint

## 0.1.1-dev — 2026-09-15

- persistent World Node and Region registries
- region lifecycle and ownership checks
- grid-coordinate collision protection

## 0.1.0-dev — 2026-09-15

- initial C++23 Core/World foundation
- initial OGL wire framing and TCP registration
