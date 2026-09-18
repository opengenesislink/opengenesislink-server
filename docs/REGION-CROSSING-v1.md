# Region Crossing v1

OpenGenesisLINK 5.0.0-dev upgrades adjacent-Region handoff to a persistent one-time transaction.

## Flow

1. The authenticated client requests `POST /v1/viewer/handoff`.
2. Core validates adjacency, moderation, Estate and Parcel policy.
3. Core prepares a short-lived Crossing record.
4. The record captures destination position, velocity, Avatar Appearance/attachment context and owned Script VM state.
5. The Crossing ID is signed into the destination Scene Ticket.
6. The destination Scene exposes the signed Crossing ID after ticket validation.
7. The client completes the transaction with `POST /v1/viewer/handoff/complete`.
8. Core atomically marks the Crossing completed and returns the transferred runtime state.

## Security

Completion is bound to:

- Crossing ID
- authenticated user
- destination Region
- expiry
- prepared state

A completed Crossing cannot be consumed again. Expired prepared Crossings are purged.

The Crossing ID is part of the HMAC-signed Scene Ticket, preventing an unsigned client from substituting another transaction.

## State carried

- position
- velocity
- Avatar Appearance and attachment references
- owned Script IDs, logical states and serialized VM states

The store accepts bounded opaque state payloads and persists them in encoded form.

## Current boundary

The Viewer still performs the final network connection switch between Scene endpoints. The transactional Core state removes the previous unsigned client-only transfer gap, but seamless server-to-server socket migration and distributed object-crossing orchestration remain later work.
