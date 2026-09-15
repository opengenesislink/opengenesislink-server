# OGL Scene Protocol v0

The development Scene endpoint uses the same 16-byte OGL1 frame header as the Core/World transport and runs on a separate TCP listener (default `127.0.0.1:19100`).

This protocol is experimental and may change without backward-compatibility guarantees.

## Connection flow

1. `HELLO` → `HELLO_ACK`
2. `SCENE_JOIN` → `SCENE_JOIN_ACK`
3. client may request snapshots/events, manipulate development objects, chat and work with terrain
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
- `TERRAIN_SET_REQUEST` (142) / `TERRAIN_SET_ACK` (143)

Payloads currently use UTF-8 key/value lines. This is a bootstrap representation, not the final high-frequency world-state format.

Objects and terrain modifications are persisted by the World Node in `0.4.0-dev`.

## Security

The Scene endpoint is still unauthenticated in v0 and intended for local development/testing only. Core Identity/Auth now exists through the Web API, but binding authenticated Core identities to Viewer/Scene sessions is a separate protocol step still to be implemented. Do not expose the Scene endpoint directly to untrusted networks.
