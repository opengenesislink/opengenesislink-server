# Wiki Handover — OpenGenesisLINK Server 10.0.0-dev

## Milestone identity

OpenGenesisLINK Server 10.0.0-dev is the Production Server Foundation milestone.

- repository: `opengenesislink/opengenesislink-server`
- pull request: #15
- final tested PR head: `c8d72075feee07fe091ceecc3f70461f125ced90`
- squash merge: `d895d8890346a12595590139254ac2e0f3a96298`
- version: `10.0.0-dev`
- implementation language: C++23
- license: MPL 2.0
- required targets: Linux x86_64, Linux ARM64/aarch64 and Windows x86_64

This document is intended as a single ingestion source for the OpenGenesisLINK Wiki. It describes the production-storage foundation introduced after the 9.0 multi-language ScriptEngine milestone.

## CI evidence

The exact final PR head `c8d72075feee07fe091ceecc3f70461f125ced90` passed every required gate before merge.

### Linux C++ CI

Workflow run #211 — run ID `35514876490`.

Ubuntu 24.04 x86_64 and Ubuntu 24.04 ARM64/aarch64 both passed:

- production database client dependency installation
- warnings-as-errors CMake configuration
- full server/test build
- unit tests
- integrated World/Social/Handoff smoke
- Script World query/ACK smoke
- OGL/LSL ScriptEngine smoke
- Crossing v3 reserve/rollback smoke
- Object Crossing end-to-end smoke

### Windows C++ CI

Workflow run #182 — run ID `35514876475`.

Windows x86_64/MSVC passed:

- MSVC environment setup
- vcpkg dependency installation/cache
- production SQLite/PostgreSQL/MariaDB client integration
- warnings-as-errors configuration
- full Core/World/test build
- unit tests

### Database Integration CI

Workflow run #32 — run ID `35514876486`.

The live relational test matrix passed against:

- SQLite
- PostgreSQL 17
- MariaDB 11.8

The database integration suite verifies migrations, prepared-parameter fidelity, transaction rollback, Identity, Sessions, World Registry, Region Registry, Moderation, Audit and Admin Role persistence.

## Storage architecture

10.0 introduces a common `DatabasePool` abstraction used by production Core stores.

Supported production backends:

| Backend | Driver/runtime | Status |
| --- | --- | --- |
| SQLite | SQLite3 | implemented and CI-tested |
| PostgreSQL | libpq | implemented and CI-tested |
| MariaDB | MariaDB Connector/C | implemented and CI-tested |

The storage layer provides:

- prepared parameters
- bounded connection pools
- per-slot synchronization
- transaction execution
- ping and reconnect handling
- successful/failed operation counters
- last-error reporting
- backend identity
- common row/scalar query surfaces

SQLite `:memory:` operation is forced to a single pooled connection so the logical database is not accidentally split across independent in-memory connections.

## SQL-authoritative Core stores

When a relational database is configured, the following services use the SQL database as their authoritative persistence layer:

- Identity
- Auth Sessions
- World Registry
- Region Registry
- Audit
- Moderation
- Admin Roles

The existing file-backed stores remain available for compatibility and development operation when the database layer is not selected.

This means PostgreSQL and MariaDB are not merely schema targets; the actual Core service implementations exercise them through the same store APIs used by SQLite.

## Schema migrations

10.0 introduces `MigrationRunner`.

The schema migration table is:

`ogl_schema_migrations`

The 10.0 foundation currently contains schema versions 1 and 2.

Version 1 creates the production foundation tables for:

- users
- auth sessions
- world nodes
- regions
- audit events
- moderation bans

Version 2 adds:

- storage metadata
- admin roles

Migration execution is idempotent. Running the migrator repeatedly leaves an already-current database unchanged.

A database reporting a schema version newer than the server understands is rejected rather than silently downgraded.

## Main SQL tables

The production foundation uses the following primary tables:

- `ogl_users`
- `ogl_auth_sessions`
- `ogl_world_nodes`
- `ogl_regions`
- `ogl_audit_events`
- `ogl_moderation_bans`
- `ogl_storage_metadata`
- `ogl_admin_roles`
- `ogl_schema_migrations`

Indexes cover session user/expiry lookups, Region node lookup, Audit time lookup and Moderation user/expiry lookup.

Region grid coordinates remain unique at database level.

## Database configuration

Core configuration now supports a `[database]` section.

Representative SQLite configuration:

```toml
[database]
backend = "sqlite"
sqlite_path = "data/core.sqlite"
pool_size = 2
connect_timeout_seconds = 5
```

Representative PostgreSQL configuration:

```toml
[database]
backend = "postgresql"
host = "127.0.0.1"
port = 5432
database = "opengenesislink"
user = "opengenesislink"
password_env = "OGL_DATABASE_PASSWORD"
pool_size = 8
connect_timeout_seconds = 5
ssl_mode = "required"
```

Representative MariaDB configuration:

```toml
[database]
backend = "mariadb"
host = "127.0.0.1"
port = 3306
database = "opengenesislink"
user = "opengenesislink"
password_env = "OGL_DATABASE_PASSWORD"
pool_size = 8
connect_timeout_seconds = 5
ssl_mode = "required"
```

The server accepts the backend aliases implemented by the parser, including PostgreSQL/Postgres/PgSQL and MariaDB/MySQL aliases.

## Production security mode

10.0 strengthens the distinction between development and production startup.

Production mode supports environment-backed secret loading for sensitive values such as:

- Scene Ticket secret
- Admin API key
- World Node authentication secret
- database password

Production startup validates secret policy and refuses known development-style credentials.

Database TLS policy is validated as part of configuration rather than being treated as an unvalidated free-form value.

Environment loading is implemented portably for Linux and Windows/MSVC.

## Health and operations

Storage state is exposed through existing and new operational surfaces.

Important endpoints:

- `GET /health`
- `GET /v1/storage/status`
- `GET /v1/status`
- `GET /metrics`

Storage health includes:

- backend
- readiness
- pool size
- successful operation count
- failed operation count
- last database error

The API capability document advertises:

- `sql-storage-v1`
- `sqlite-storage-v1`
- `postgresql-storage-v1`
- `mariadb-storage-v1`
- `storage-health-v1`
- `admin-rbac-v1`
- `auth-rate-limit-v1`

## Storage CLI

10.0 adds the `opengenesis-storage` utility.

It can inspect the configured backend and schema/storage status without requiring an operator to infer database state from application logs.

The production foundation smoke verifies that the CLI reports the expected SQLite backend, readiness and schema version.

## Administrative roles

The new SQL-backed Admin Role store persists role grants.

The production foundation exercises at least the operational role boundary used by administrative APIs, including Moderator and Operator access checks.

Role data is stored in `ogl_admin_roles`.

## Rate limiting and production auth hardening

The Core production foundation includes bounded registration/login rate limiting.

The process smoke verifies repeated failed login attempts transition from ordinary authentication failure to an explicit rate-limit response.

This is a server-side protection and does not depend on a Viewer implementation.

## MariaDB interoperability fix

Live MariaDB CI exposed a result-decoding bug that SQLite and PostgreSQL did not reveal.

The MariaDB prepared-result path originally used a one-byte scratch result buffer and then fetched the real column value. Numeric columns can be reported with binary/native metadata semantics, causing incorrect readback when handled like an already-complete one-byte text field.

The final 10.0 implementation corrects MariaDB numeric result decoding and the live Region persistence test verifies that numeric metrics such as entity counts and `sim_fps` survive write/read cycles correctly.

This is why the MariaDB CI gate is important: compile-only support would not have detected this class of backend-specific defect.

## Cross-platform status

10.0.0-dev is verified on:

- Linux x86_64
- Linux ARM64/aarch64
- Windows x86_64/MSVC

The production database dependency layer is part of all three build targets.

ARM64 remains a first-class target for deployments such as Raspberry Pi 5 where service scale permits it.

## Compatibility with 9.0

10.0 does not replace the 9.0 ScriptEngine architecture.

The following remain part of the server foundation:

- shared ScriptEngine IR
- legacy/LSL/OGL language frontends
- Script VM budgets and persistence
- ScriptHost policy layer
- World action queue with ACK/NACK/retry/expiry
- Crossing v3
- Object Crossing v2
- native Physics/Object Runtime
- OGL-FED and Hypergrid compatibility layers
- Social/Groups/Land/Assets/Inventory services

10.0 primarily makes the Core persistence/security/operations layer suitable for real multi-process server deployments.

## What 10.0 does not claim

`10.0.0-dev` is still a development milestone.

It does not claim:

- stable 1.0 production API compatibility
- complete LSL semantic compatibility
- completed Viewer implementation
- completed Atlas implementation
- completed hosted Voice service
- globally distributed/high-availability database clustering
- automatic multi-datacenter consensus
- zero-downtime schema upgrades across mixed server versions

Those remain later roadmap work.

## Final milestone state

Production Server Foundation acceptance criteria are satisfied:

- SQLite production backend: PASS
- PostgreSQL production backend: PASS
- MariaDB production backend: PASS
- live database integration tests: PASS
- Linux x86_64: PASS
- Linux ARM64/aarch64: PASS
- Windows x86_64/MSVC: PASS
- warnings-as-errors: PASS
- migrations: PASS
- SQL-backed Core stores: PASS
- production secret validation: PASS
- storage health/metrics: PASS
- PR #15 merged: PASS
- final Wiki handover committed to `main`: this document

The canonical 10.0 merge commit is:

`d895d8890346a12595590139254ac2e0f3a96298`
