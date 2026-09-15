# OGL Scene Protocol v0

The development Scene endpoint uses the same 16-byte OGL1 frame header as the Core/World transport and runs on a separate TCP listener (default `127.0.0.1:19100`).

This protocol is experimental and may change without backward-compatibility guarantees.

## Authenticated connection flow

1. Client authenticates to Core HTTP API.
2. Client requests `POST /v1/viewer/session` for a Region.
3. Core returns the World Scene endpoint and a short-lived signed Scene Ticket.
4. Client opens Scene TCP and sends `HELLO` → `HELLO_ACK`.
5. Client sends `SCENE_JOIN` containing `region=<id>` and `ticket=<ogst1...>`.
6. World validates signature, expiry, Region binding and replay nonce before creating Avatar Presence.
7. `GOODBYE` or disconnect removes the temporary Avatar Presence.

The Avatar display name and account id come from the signed Core ticket, not from untrusted client fields.

## Scene messages

- `SCENE_JOIN` (100) / `SCENE_JOIN_ACK` (101)
- `SCENE_SNAPSHOT_REQUEST` (102) / `SCENE_SNAPSHOT` (103)
- `ENTITY_CREATE` (110) / `ENTITY_CREATE_ACK` (111)
- `ENTITY_UPDATE` (112) / `ENTITY_UPDATE_ACK` (113)
- `ENTITY_DELETE` (114) / `ENTITY_DELETE_ACK` (115)
- `CHAT_SEND` (120) / `CHAT_EVENT` (121)
- `SCENE_EVENTS_REQUEST` (130) / `SCENE_EVENTS` (131)
- `TERRAIN_SAMPLE_REQUEST` (140) / `TERRAIN_SAMPLE` (141)
- `TERRAIN_SET_REQUEST` (142) / `TERRAIN_SET_ACK` (143)

Payloads currently use UTF-8 key/value lines. This is bootstrap encoding rather than the final high-frequency world-state representation.

## Ownership

Objects created through an authenticated Scene session receive the account id from that session as owner. In `0.5.0-dev`, object update/delete through this Scene connection requires matching ownership. Object ownership is persisted with scene-object state.

Terrain editing is authenticated but does not yet have role/capability authorization.

## Security limits

Ticket replay is rejected by an in-memory World-process nonce cache. A World process restart resets that cache, so distributed/persistent replay state remains future work. Keep the Scene listener on a trusted interface until TLS, roles and capability authorization are complete.
