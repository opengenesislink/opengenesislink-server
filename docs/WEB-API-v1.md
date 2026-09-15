# OpenGenesisLINK Core Web API v1

`0.5.0-dev` extends the Core HTTP listener into a development Web/API surface covering runtime status, Identity/Auth, Viewer session handoff, Assets and Inventory. The default bind address remains `127.0.0.1:18080`.

## Browser dashboard

`GET /` returns an HTML dashboard showing:

- Core version and uptime
- connected World Nodes and generations
- Regions and grid coordinates
- simulation FPS, entity/avatar and physics counts
- terrain revision
- identity and active-session counts
- Asset and Inventory counts

## Discovery and status

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/health` | lightweight health check |
| GET | `/v1` | API discovery and capability document |
| GET | `/v1/status` | combined Core/World/Region/content state |
| GET | `/v1/worlds` | World Node list |
| GET | `/v1/regions` | Region list and live metrics |
| GET | `/v1/identity/stats` | identity/session counters |
| GET | `/v1/content/stats` | Asset/Inventory counters |

## Identity

`POST /v1/auth/register` and `POST /v1/auth/login` return a bearer token. `GET /v1/auth/me` resolves that token and `POST /v1/auth/logout` revokes it.

Passwords are stored as PBKDF2-HMAC-SHA256 verifiers with random salts. Raw bearer tokens are never persisted; `sessions.db` stores SHA-256 token hashes.

## Viewer session handoff

`POST /v1/viewer/session`

```text
Authorization: Bearer <token>
Content-Type: application/json
```

```json
{
  "region": "genesis-central"
}
```

The response contains the resolved Scene endpoint plus a short-lived region-specific Scene Ticket. The client supplies that ticket to `SCENE_JOIN`.

## Assets

Authenticated endpoints:

- `GET /v1/assets`
- `POST /v1/assets`
- `GET /v1/assets/<asset-id>`

Upload body:

```json
{
  "name": "example.txt",
  "mime_type": "text/plain",
  "data_base64": "..."
}
```

## Inventory

Authenticated endpoints:

- `GET /v1/inventory`
- `POST /v1/inventory/folders`
- `POST /v1/inventory/items`

Inventory items reference Asset ids and do not duplicate Asset payload data.

## Security status

This remains a development interface. The listener is loopback-only by default. Scene Tickets now establish an authenticated handoff to the World Node, but TLS, roles, fine-grained capabilities, rate limiting and production key management are not yet complete.
