# OGL Scene Protocol v0

`0.3.0-dev` introduces the first development Scene endpoint on each World Node. It uses the same 16-byte OGL1 binary frame header as the Core/World transport but runs on a separate TCP listener (default `127.0.0.1:19100`).

This protocol is intentionally experimental and may change without backward-compatibility guarantees.

## Connection flow

1. `HELLO` → `HELLO_ACK`
2. `SCENE_JOIN` → `SCENE_JOIN_ACK`
3. client may request snapshots/events, manipulate development objects, chat and sample terrain
4. `GOODBYE` or TCP disconnect removes the temporary avatar presence

## Scene messages

- `SCENE_JOIN` (100) / `SCENE_JOIN_ACK` (101)
- `SCENE_SNAPSHOT_REQUEST` (102) / `SCENE_SNAPSHOT` (103)
- `ENTITY_CREATE` (110) / `ENTITY_CREATE_ACK` (111)
- `ENTITY_UPDATE` (112) / `ENTITY_UPDATE_ACK` (113)
- `ENTITY_DELETE` (114) / `ENTITY_DELETE_ACK` (115)
- `CHAT_SEND` (120) / `CHAT_EVENT` (121)
- `SCENE_EVENTS_REQUEST` (130) / `SCENE_EVENTS` (131)
- `TERRAIN_SAMPLE_REQUEST` (140) / `TERRAIN_SAMPLE` (141)

Payloads currently use UTF-8 key/value lines. This is a bootstrap representation, not the final high-frequency world-state format.

## Security

The v0 Scene endpoint is unauthenticated and intended for local development/testing only. It must not be exposed directly to untrusted networks. Authentication, capability negotiation, rate limits and encrypted transport are later protocol layers.
