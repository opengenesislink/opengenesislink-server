# Authenticated Viewer Flow v0

OpenGenesisLINK `0.5.0-dev` introduces the first authenticated path from Core identity to World Scene.

## Flow

1. A client registers or logs in through Core HTTP API.
2. Core returns a persistent bearer session token.
3. The client requests `POST /v1/viewer/session` with the bearer token and a target Region id.
4. Core verifies the account session, resolves the online Region and its World Node, and creates a short-lived Scene Ticket.
5. The client connects to the returned Scene endpoint and sends `SCENE_JOIN` with that ticket.
6. The World Node validates signature, expiry and Region binding, then consumes the ticket nonce and creates Avatar Presence.

The World Node does not trust a client-supplied avatar display name. Account id and display name come from signed ticket claims.

## Scene Ticket

Development token prefix: `ogst1.`

The token contains signed claims for:

- user id
- display name
- Region id
- random nonce
- issued timestamp
- expiration timestamp

The signature uses HMAC-SHA256 with the shared `security.scene_ticket_secret` configured on Core and World Node.

Ticket lifetime is constrained to 10–300 seconds. The default Core setting is 60 seconds.

A nonce may only be consumed once by a running World process. Replay attempts are rejected. Persistent/distributed replay state is not implemented yet, so a World process restart resets the in-memory replay cache.

## Configuration

Core:

```toml
[identity]
scene_ticket_lifetime_seconds = 60

[security]
scene_ticket_secret = "replace-with-a-long-random-secret"
```

World Node:

```toml
[security]
scene_ticket_secret = "replace-with-the-same-long-random-secret"
```

The repository default is intentionally marked as a development secret and must be replaced before network exposure.

## Current security boundary

Implemented:

- password hashing
- persistent bearer sessions
- signed Scene Tickets
- ticket Region binding and expiry
- runtime replay rejection
- authenticated Avatar identity
- basic scene-object ownership

Not yet production complete:

- TLS termination
- key rotation / asymmetric signing
- distributed replay state
- roles and granular capabilities
- rate limiting / abuse controls
- remote federation identity
