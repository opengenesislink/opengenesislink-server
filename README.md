# OpenGenesisLINK Server

OpenGenesisLINK is an independent C++23 server platform for federated virtual worlds. It is not an OpenSimulator fork. Legacy/OpenSim interoperability is intended to live behind explicit compatibility adapters.

Current development version: **0.3.0-dev**.

## What already runs

- Core and World Node processes
- binary OGL foundation framing over TCP
- persistent World Node and Region registries
- generation-based reconnect sessions and node leases
- region registration and lifecycle
- live Region Runtime with dedicated tick loop
- 256×256 heightfield terrain foundation
- Scene entities with position, rotation and scale transforms
- avatar presence as runtime entities
- object create/update/delete operations
- sequenced local scene event stream and local chat events
- first TCP Scene endpoint for development clients
- native OpenGenesis Physics kernel with terrain-aware ground contact
- region runtime metrics streamed to the Core
- HTTP health/status API
- Region Runtime continues while the Core is temporarily unavailable
- automatic World Node reconnect after Core restart
- Debug and Release tests plus end-to-end scene/recovery smoke test

## Build

Requirements: Linux, CMake >= 3.25, Ninja, a C++23 compiler and pthreads.

```bash
./scripts/build.sh
```

Run the end-to-end scene/recovery test:

```bash
./scripts/smoke-test.sh
```

Default endpoints:

- Core World-Node transport: `127.0.0.1:19000`
- World Scene development endpoint: `127.0.0.1:19100`
- Core status API: `http://127.0.0.1:18080/v1/status`

The Scene endpoint is an early development protocol and has no authentication yet. Do not expose it to untrusted networks.

## Supported architecture targets

OpenGenesisLINK is developed without intentional x86-only dependencies. The primary targets are x86_64 and ARM64/aarch64.

## License

Mozilla Public License 2.0 (MPL-2.0). See `LICENSE`.
