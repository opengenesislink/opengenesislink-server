# OpenGenesisLINK Server

OpenGenesisLINK is an independent **C++23** server platform for federated virtual worlds. It is not an OpenSimulator fork. Legacy/OpenSim interoperability is intended to live behind explicit compatibility adapters.

Current development version: **0.4.0-dev**.

## What already runs

- Core and World Node processes
- binary OGL foundation framing over TCP
- persistent World Node and Region registries
- generation-based reconnect sessions and node leases
- live Region Runtime with dedicated tick loop
- 256×256 terrain runtime
- scene entities, avatar presences, object create/update/delete and local chat
- first native OpenGenesis Physics kernel
- persistent scene objects and terrain per region
- automatic World Node reconnect after Core restart
- persistent Core identities and login sessions
- PBKDF2-HMAC-SHA256 password hashing via OpenSSL
- browser-readable Core dashboard and JSON API
- HTTP health, status, world, region, identity and auth endpoints
- x86_64 and ARM64/aarch64 CI

## Browser interface

Start the Core and open:

```text
http://127.0.0.1:18080/
```

The development dashboard shows the current Core version, uptime, World Nodes, Regions, avatars, runtime metrics, identities and active login sessions. The API index is available at:

```text
http://127.0.0.1:18080/v1
```

The admin/API listener binds to loopback by default. Do not expose the current development HTTP endpoint directly to an untrusted network; TLS, authorization levels and production hardening are later layers.

## Build

Requirements: Linux, CMake >= 3.25, Ninja, a C++23 compiler, pthreads and OpenSSL development headers.

Debian/Ubuntu example:

```bash
sudo apt install build-essential cmake ninja-build libssl-dev
./scripts/build.sh
```

Run the full end-to-end test:

```bash
./scripts/smoke-test.sh
```

The smoke test covers Identity/Auth, Web API, Scene protocol, persistent objects, persistent terrain, Core restart recovery and World restart recovery.

## Supported architecture targets

OpenGenesisLINK is developed without intentional x86-only dependencies. The primary targets are x86_64 and ARM64/aarch64.

## License

Mozilla Public License 2.0 (MPL-2.0). See `LICENSE`.
