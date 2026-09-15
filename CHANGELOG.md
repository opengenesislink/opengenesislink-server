# Changelog

## 0.2.0-dev — 2026-09-15

First larger runtime milestone.

- added generation-based World Node reconnect sessions
- added node lease renewal and stale-node expiry foundation
- added automatic World Node reconnect after Core restart
- added live Region Runtime with configurable tick rate
- added entity and avatar foundation
- added native OpenGenesis Physics kernel with rigid-body gravity integration and ground contact
- added region runtime metrics (ticks, FPS, entities, avatars, physics bodies)
- added metrics transport to Core
- added HTTP `/health` and `/v1/status` endpoints
- preserved persistent World Node and Region registries
- retained region lifecycle and grid-coordinate collision protection
- expanded unit and recovery smoke tests
- public repository reduced to source, configuration and technical documentation only

## 0.1.1-dev — 2026-09-15

- persistent World Node and Region registries
- region lifecycle and ownership checks
- grid-coordinate collision protection

## 0.1.0-dev — 2026-09-15

- initial C++23 Core/World foundation
- initial OGL wire framing and TCP registration
