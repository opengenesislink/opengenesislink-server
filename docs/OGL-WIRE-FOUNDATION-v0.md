# OGL Wire Foundation v0

OpenGenesisLINK currently uses a small binary frame envelope for early protocol work.

Header size: 16 bytes, network byte order.

- bytes 0..3: magic `OGL1`
- bytes 4..5: protocol version
- bytes 6..7: message type
- bytes 8..11: request id
- bytes 12..15: payload length
- remaining bytes: payload

Maximum payload: 1 MiB.

Two transports currently use this frame:

1. Core ↔ World Node control-plane connection (registration, leases, region state, metrics, Script World-action delivery)
2. World Node Scene development endpoint (join, snapshots, entities, chat, events, terrain samples)

The Script World control path defines `script_action_poll`, `script_action`, `script_action_result` and `script_action_result_ack`. A World Node may request queued Script actions only for Regions registered to its current node generation. Since 6.5, polling leases a durable action rather than deleting it; the World Node reports success/rejection and Core ACKs or schedules retry. Query results are base64-wrapped inside the bootstrap key/value envelope. The payload is still a UTF-8 key/value bootstrap format. The wire format and message schemas are not frozen and currently carry no backward-compatibility guarantee.
