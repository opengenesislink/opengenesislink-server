# OGL-FED Foundation v0

OGL-FED is the native OpenGenesisLINK federation path. It is separate from the OpenSimulator Hypergrid compatibility bridge.

Protocol identifier: `OGL-FED/1`.

## Security model

OGL-FED does not use a shared Grid password.

Each Grid owns an Ed25519 key pair. Remote peers are identified by Grid ID, HTTPS base URL and public key. Trust and revocation are persisted by the local Grid.

Travel authorization uses a short-lived signed token containing:

- issuer Grid
- audience / destination Grid
- subject user
- display name
- origin Region
- destination Region
- session ID
- cryptographically random nonce
- issued-at time
- expiry time

The destination verifies the Ed25519 signature, issuer, audience and lifetime. A Travel nonce is consumed once and rejected on replay.

## Trust

The 2.5 foundation includes a persistent peer trust store with explicit revocation. A revoked peer is not accepted merely because it presents a previously known public key.

HTTPS is required for peers added through the trust-store API foundation. Certificate validation and transport policy belong to the HTTP/TLS transport layer and remain part of later production hardening.

## Crossing transaction foundation

The persistent Crossing Store records:

- one Crossing ID
- user binding
- source and destination Region
- position
- velocity
- prepared/completed/aborted state
- expiry
- one-time completion

This creates the transaction primitive needed for safer Region handoff. It is deliberately independent from Viewer protocol details.

## Hypergrid separation

OpenSimulator Hypergrid support is a legacy interoperability adapter. Hypergrid requests, service sessions and legacy callbacks must not be treated as OGL-FED Travel Tokens and OGL-FED keys must not be reused as Hypergrid shared secrets.

## Current milestone boundary

2.5.0-dev provides the cryptographic, trust, replay and crossing foundations. It does not claim:

- complete multi-Grid production federation
- complete remote Inventory/Asset federation
- complete Federation Friends/Presence
- complete OpenSimulator Hypergrid interoperability
- complete Viewer/Simulator legacy compatibility
