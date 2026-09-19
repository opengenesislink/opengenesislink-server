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

1. Core ↔ World Node control-plane connection (registration, leases, region state, metrics, Script World-action delivery and Object Crossing orchestration)
2. World Node Scene development endpoint (join, snapshots, entities, chat, events, terrain samples)

The Script World control path defines `script_action_poll`, `script_action`, `script_action_result` and `script_action_result_ack`. A World Node may request queued Script actions only for Regions registered to its current node generation. Since 6.5, polling leases a durable action rather than deleting it; the World Node reports success/rejection and Core ACKs or schedules retry. Query results are base64-wrapped inside the bootstrap key/value envelope. The payload is still a UTF-8 key/value bootstrap format. The wire format and message schemas are not frozen and currently carry no backward-compatibility guarantee.


## Object Crossing v1 control messages

OpenGenesisLINK 7.5 adds four Core ↔ World control-plane message types:

- `object_crossing_poll` (45)
- `object_crossing_command` (46)
- `object_crossing_result` (47)
- `object_crossing_result_ack` (48)

A World Node polls once per local Region. Core validates that the node owns that Region in its current generation before returning a command.

The command sequence for a successful single-object crossing is:

```text
source World      Core                 destination World
    |              |                         |
    |-- poll ------>|                         |
    |<- export -----|                         |
    |-- snapshot -->|                         |
    |              |<--------- poll ---------|
    |              |---------- import ------>|
    |              |<--------- import ACK ---|
    |-- poll ------>|                         |
    |<- remove -----|                         |
    |-- remove ACK->|                         |
    |              |                         |
    |          completed                      |
```

Rollback after destination import uses:

```text
cleanup destination -> restore source -> rolled_back
```

The restore phase is deliberately retained even when the source object appears to still exist. This makes the flow safe when a source removal succeeded but its result acknowledgement was lost.

Object snapshots are Base64-wrapped inside the current UTF-8 key/value bootstrap envelope and are bounded to 64 KiB. They currently carry single-object identity/permission/transform data, physical state and linear velocity.

The wire schema is still pre-stable and is not a compatibility promise.
