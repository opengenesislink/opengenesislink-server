# ScriptHost v1

OpenGenesisLINK 5.5.0-dev adds a policy-controlled host layer between the sandboxed Script VM and native server services.

## Principle

The Script VM does not receive direct references to Core stores, sockets, files, processes or databases. It emits typed actions. ScriptHost decides whether those actions may be applied.

This keeps Script language execution separate from authorization.

## Current host actions

### notify

Source:

```text
notify Your scripted event completed
```

Behavior:

- creates a native Notification for the Script owner
- Notification type: `script`
- target: Script ID
- text remains subject to normal Notification sanitization

### message

Source:

```text
message <recipient-user-id> Hello from this Script
```

Behavior:

- recipient must be an existing local OpenGenesisLINK user
- recipient must be an accepted friend of the Script owner
- message is persisted through the native MessageStore
- recipient receives a native direct-message Notification
- non-friends and unknown users are rejected by host policy

## Execution paths

ScriptHost is used for:

- authenticated `POST /v1/scripts/event`
- automatic timer events from the Core maintenance loop

The API response reports:

- VM actions emitted
- number of host actions applied
- host-policy errors

## Security boundary

ScriptHost does not expose arbitrary Core service access. Adding a new Script capability should require a new explicit action type and a host-side policy check.

Not implemented yet:

- arbitrary HTTP-out
- filesystem/process execution
- Inventory mutation from Scripts
- Group/Land mutation from Scripts
- unrestricted object/world mutation
- permission-dialog workflow
- per-Region/per-owner production quotas
