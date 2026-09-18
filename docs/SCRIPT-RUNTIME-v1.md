# Script Runtime v1

OpenGenesisLINK 5.0.0-dev introduces a small deterministic server-side event VM. It is intentionally not a general-purpose native-code runtime.

## Security model

Scripts do not receive direct filesystem, process, socket or database access. The VM produces explicit host actions which the OpenGenesisLINK runtime may apply after policy checks.

Every execution is bounded by:

- instruction budget
- variable count
- persistent state byte limit
- output action count
- source size and handler size limits

A budget violation stops the event without committing the new VM state.

## Source format

The v1 source format is line-oriented:

```text
event touch
set count 1
add count 2
emit $count
state active
timer 1500
listen 7
end
```

Supported instructions:

- `set <name> <value>`
- `add <name> <integer>`
- `emit <value>`
- `state <name>`
- `timer <milliseconds>`
- `listen <channel>`
- `stop`

A value beginning with `$` resolves a persistent variable.

## Persistence

The Script Runtime persists:

- Script ID
- object ID
- owner user ID
- SHA-256 source hash
- source text
- serialized VM state
- logical Script state
- timer configuration
- listen channel configuration
- event counter

Old v1 Script Runtime records without source/VM state remain readable.

## Core API

Authenticated development endpoints:

- `GET /v1/scripts`
- `POST /v1/scripts`
- `POST /v1/scripts/event`

Due timers are dispatched by the Core maintenance loop.

## Current boundary

This is the native OpenGenesisLINK Script VM foundation. Full LSL compatibility, richer world APIs, HTTP-out, inventory/group/land functions, permission prompts and distributed World-Node execution remain later work.
