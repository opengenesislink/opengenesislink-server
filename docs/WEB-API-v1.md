# OpenGenesisLINK Core Web API v1

`0.4.0-dev` expands the Core HTTP listener into a development Web/API surface. The default bind address remains `127.0.0.1:18080`.

## Browser dashboard

`GET /` returns an HTML dashboard showing live server information. It polls the JSON API and displays:

- Core version and uptime
- connected World Nodes and generations
- Regions and grid coordinates
- simulation FPS, ticks and entity counts
- avatar and physics-body counts
- terrain revision
- identity and active-session counts

## Discovery and status

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/health` | lightweight health check |
| GET | `/v1` | API discovery document |
| GET | `/v1/status` | combined Core/World/Region runtime state |
| GET | `/v1/worlds` | World Node list |
| GET | `/v1/regions` | Region list and live metrics |
| GET | `/v1/identity/stats` | identity/session counters |
| GET | `/v1/identity/users` | safe identity metadata; never password hashes |

## Identity and authentication

### Register

`POST /v1/auth/register`

```json
{
  "username": "alice.avatar",
  "display_name": "Alice Avatar",
  "password": "a sufficiently long password"
}
```

A successful registration returns HTTP `201` with a user object, bearer token and expiry time.

### Login

`POST /v1/auth/login`

```json
{
  "username": "alice.avatar",
  "password": "a sufficiently long password"
}
```

### Current identity

`GET /v1/auth/me`

```text
Authorization: Bearer <token>
```

### Logout

`POST /v1/auth/logout` with the same Bearer header revokes the session.

## Storage and password handling

Passwords are not stored directly. `users.db` stores PBKDF2-HMAC-SHA256 password verifiers with a random salt. Session bearer tokens are returned only to the client; `sessions.db` stores SHA-256 token hashes rather than raw bearer tokens.

## Security status

This is a development API, not a production security boundary. The listener is loopback-only by default. TLS termination, roles/permissions, rate limiting, CSRF/browser-session policy and the final Viewer-to-Identity authentication flow are not yet complete.
