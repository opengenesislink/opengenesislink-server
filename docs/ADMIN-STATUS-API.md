# Admin Status API

The Core exposes a minimal read-only HTTP status service.

- `GET /health` — process health and version
- `GET /v1/status` — World Nodes, session generations, Regions and runtime metrics

The 0.2.0-dev endpoint is intentionally local by default (`127.0.0.1:18080`) and has no authentication. Do not expose it publicly. Authentication/TLS belongs to a later hardening milestone.
