# Script Runtime v1

OpenGenesisLINK 6.0.0-dev develops a small deterministic server-side event VM. It is intentionally not a general-purpose native-code runtime.

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
- `notify <text>`
- `message <recipient-user-id> <text>`
- `move <x> <y> <z>`
- `rotate <x> <y> <z>`
- `scale <x> <y> <z>`
- `physics <0|1>`
- `say <text>`
- `whisper <text>`
- `shout <text>`
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

Due timers are dispatched by the Core maintenance loop and their host actions are passed through the same ScriptHost policy layer as manually invoked events.

## ScriptHost policy layer

The VM itself never writes directly to Social or Notification storage. It emits typed actions.

Current host-controlled actions:

- `notify`: create a native Notification for the Script owner
- `message`: send a native direct message only when the recipient is an existing local user and an accepted friend of the Script owner
- transform/physics/chat actions: route through ScriptHost and the bounded Core-to-World action path; the World Node revalidates Region and object ownership

Rejected host actions are returned as policy errors without granting the Script broader service access.

## Current boundary

This is the native OpenGenesisLINK Script VM foundation. 6.0 adds the first distributed World-Node action path, but object reads/queries, richer Scene actions, full LSL compatibility, HTTP-out, inventory/group/land mutation functions, permission prompts, action acknowledgement/retry and broader distributed execution remain later work.
