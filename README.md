# OpenGenesisLINK Server

OpenGenesisLINK is an independent C++23 server platform for federated virtual worlds. It is not an OpenSimulator fork. Legacy/OpenSim interoperability is intended to live behind explicit compatibility adapters.

Current development version: **0.2.0-dev**.

## What already runs

- Core and World Node processes
- binary OGL foundation framing over TCP
- persistent World Node and Region registries
- generation-based reconnect sessions
- node leases and stale-node handling
- region registration and lifecycle
- live Region Runtime with dedicated tick loop
- entity/avatar foundation
- first native OpenGenesis Physics kernel
- region runtime metrics streamed to the Core
- HTTP health/status API
- automatic World Node reconnect after Core restart
- Debug and Release tests

## Build

Requirements: Linux, CMake >= 3.25, Ninja, a C++23 compiler and pthreads.

```bash
./scripts/build.sh
```

Run an end-to-end recovery test:

```bash
./scripts/smoke-test.sh
```

The default status endpoint is `http://127.0.0.1:18080/v1/status`.

## Supported architecture targets

OpenGenesisLINK is developed without intentional x86-only dependencies. The primary targets are x86_64 and ARM64/aarch64.

## License

Mozilla Public License 2.0 (MPL-2.0). See `LICENSE`.
