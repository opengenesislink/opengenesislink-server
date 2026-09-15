# OGL Wire Foundation v0

The current transport is intentionally a development protocol and carries no backwards-compatibility guarantee yet.

Each frame has a 16-byte big-endian header:

- 4 bytes magic: `OGL1`
- 2 bytes protocol version
- 2 bytes message type
- 4 bytes request id
- 4 bytes payload length

The payload is currently a UTF-8 key/value envelope. The maximum payload is 1 MiB.

Implemented message groups: handshake, World registration, leases, Region registration, Region lifecycle, runtime metrics, ping/pong and graceful disconnect.
