# Changelog

## 0.3.0-dev — 2026-09-15

First Scene Runtime milestone.

- added persistent World-side Region Runtime independent of Core reconnects
- added 256×256 heightfield Terrain foundation with revision tracking and bilinear sampling
- connected OpenGenesis Physics ground contact to Region Terrain
- expanded entities to typed objects/avatars with position, rotation and scale transforms
- added object create/update/delete and velocity operations
- added avatar presence as Scene entities
- added sequenced Scene event journal with bounded retention
- added local chat events
- added TCP Scene development endpoint on the World Node
- added SCENE_JOIN, snapshots, scene-event polling and terrain sampling
- added ENTITY_CREATE/UPDATE/DELETE and CHAT_SEND protocol messages
- added scene-event and terrain revision metrics to Core status
- expanded unit tests for Terrain, Scene Runtime and terrain-aware Physics
- expanded smoke test into a real Scene client plus Core restart/reconnect test

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
