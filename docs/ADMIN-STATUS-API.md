# Core Status API

The Core exposes a small development HTTP status endpoint (default `127.0.0.1:18080`).

- `GET /health` — process health and version
- `GET /v1/status` — World Node and Region state

Region status includes generation, lifecycle state, ticks, entities, avatars, physics bodies, Scene event sequence count, Terrain revision and simulation FPS.

The endpoint is currently operational/diagnostic only and is not yet the final authenticated administration API.
