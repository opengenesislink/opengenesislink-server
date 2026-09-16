# Authenticated Viewer Flow v0

OpenGenesisLINK `1.5.0-dev` uses a Core-issued Scene Ticket to connect an authenticated account to a World Region.

## Flow

1. A client registers or logs in through Core HTTP API.
2. Core returns a persistent bearer session token.
3. The client requests `POST /v1/viewer/session` with a target Region id.
4. Core verifies account session and Region availability and creates a short-lived Scene Ticket.
5. The client connects to the returned Scene endpoint and sends `SCENE_JOIN` with the ticket.
6. The World Node validates signature, expiry, Region binding, capability claims and nonce replay.
7. Avatar Presence is created from authenticated ticket identity, not from client-supplied avatar identity.

## Scene Ticket

Development token prefix: `ogst1.`

Signed claims include:

- user id
- display name
- Region id
- random nonce
- Scene capability list
- optional handoff source Region
- issued timestamp
- expiration timestamp

The signature uses HMAC-SHA256 with `security.scene_ticket_secret` configured on Core and World Node.

Ticket lifetime is constrained to 10–300 seconds. The default Core setting is 60 seconds.

A nonce is consumed once by a running World process. A World restart currently resets the in-memory replay cache.

## Capabilities

The default development ticket contains explicit capabilities for Scene join/read, movement, chat, own-object editing and terrain access. The World Node checks those capabilities for every corresponding Scene operation.

The capability mechanism is implemented; final role, land/parcel and administrator policy is not.

## Region handoff

`POST /v1/viewer/handoff` issues a new target-Region ticket only when the target is a known online cardinal neighbor. The ticket carries `handoff_from` so the target Scene can distinguish a handoff from a normal login.

## Current security boundary

Implemented:

- password hashing
- persistent bearer sessions
- signed Scene Tickets
- Region binding and expiry
- runtime replay rejection
- authenticated Avatar identity
- Scene capabilities
- basic scene-object ownership
- adjacent-Region handoff ticketing

Not yet production complete:

- TLS termination
- key rotation / asymmetric signing
- distributed replay state
- land/parcel permission policy
- rate limiting / abuse controls
- remote federation identity
