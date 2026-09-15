# OpenGenesisLINK Server

OpenGenesisLINK is an independent **C++23** server platform for federated virtual worlds. It is not an OpenSimulator fork. Legacy/OpenSim interoperability is intended to live behind explicit compatibility adapters.

Current development version: **0.5.0-dev**.

## What already runs

- Core and World Node processes
- binary OGL framing over TCP
- persistent World Node and Region registries
- generation-based reconnect sessions and leases
- live Region Runtime with dedicated tick loop
- 256×256 terrain runtime
- persistent scene objects and terrain
- native OpenGenesis Physics foundation
- persistent identities and bearer sessions
- PBKDF2-HMAC-SHA256 password hashing via OpenSSL
- browser-readable Core dashboard and JSON API
- short-lived HMAC-signed Scene Tickets
- authenticated Avatar Presence: display name and account id come from Core identity
- Scene Ticket replay protection for the running World process
- ownership on newly created scene objects
- persistent Asset metadata plus content-addressed blob storage
- persistent Inventory roots, folders and items
- x86_64 and ARM64/aarch64 CI

## Browser interface

Start Core and World, then open:

```text
http://127.0.0.1:18080/
```

The development dashboard shows Core, World/Region, simulation, identity/session and content-service information.

API discovery:

```text
http://127.0.0.1:18080/v1
```

Important development endpoints include:

```text
POST /v1/auth/register
POST /v1/auth/login
POST /v1/viewer/session
GET  /v1/status
GET  /v1/regions
GET  /v1/assets
POST /v1/assets
GET  /v1/inventory
POST /v1/inventory/folders
POST /v1/inventory/items
```

The admin/API and Scene listeners bind to loopback by default. Replace the development Scene Ticket secret before exposing anything beyond a trusted host. TLS, roles and fine-grained capabilities are not complete yet.

## Authenticated viewer flow

1. Authenticate with the Core and obtain a bearer session.
2. Request a region-specific Scene Ticket using `POST /v1/viewer/session`.
3. Connect to the returned Scene endpoint.
4. Send `SCENE_JOIN` with the ticket.
5. The World Node verifies the Core signature and region binding before creating Avatar Presence.

The client-provided avatar name is not trusted. The World Node uses the identity embedded in the signed ticket.

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

The smoke test covers Identity/Auth, Scene Ticket issuance and replay rejection, authenticated Scene join, Assets, Inventory, persistent objects/terrain, Core restart recovery and full World Node restart recovery.

## Documentation

- `docs/BUILD.md`
- `docs/OGL-WIRE-FOUNDATION-v0.md`
- `docs/SCENE-PROTOCOL-v0.md`
- `docs/VIEWER-AUTH-v0.md`
- `docs/CONTENT-SERVICES-v0.md`
- `docs/WEB-API-v1.md`
- `docs/REGION-RUNTIME.md`
- `docs/REGION-PERSISTENCE-v0.md`
- `docs/PHYSICS.md`

## License

Mozilla Public License 2.0. See `LICENSE`.
