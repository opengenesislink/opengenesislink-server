# OGL ScriptEngine v2

OpenGenesisLINK 9.0.0-dev introduces a multi-language ScriptEngine instead of growing independent language runtimes.

## Architecture

All supported source languages compile into the same bounded Script IR and execute through the same VM, persistent Script Runtime, ScriptHost policy layer and durable Core-to-World action queue.

```text
Legacy source ----\
LSL source --------> language frontend -> shared Script IR -> bounded VM
OGL source -------/                                      |
                                                         v
                                                  typed host actions
                                                         |
                                         +---------------+--------------+
                                         |                              |
                                      Core host                      World queue
                                         |                              |
                                         v                              v
                              social/notifications              owning World Node
```

The language frontend never receives direct database, filesystem, process or socket access.

## Languages

Persistent Script records expose one of:

- `legacy` — the original line-oriented internal source syntax, kept for compatibility
- `lsl` — Second Life LSL compatibility frontend
- `ogl` — native OpenGenesisLINK scripting language

Language selection is stored in Script Runtime persistence v3 and survives Core restart and Object Crossing rebinding.

## State-aware handlers

Script IR handlers now carry:

- logical state
- event name
- ordered event parameter names
- instructions

Legacy handlers use wildcard state `*`.

LSL and OGL handlers are state-specific.

## Event payload binding

A bounded event payload is newline-delimited and mapped in order onto handler parameter names.

Example LSL:

```lsl
listen(integer channel, string name, key id, string message)
```

Payload:

```text
7
Alice
agent-key
Hello world
```

VM variables become `channel`, `name`, `id` and `message`.

## Security budgets

Every language shares the existing limits:

- source size
- handler size
- instruction budget
- variable count
- VM state bytes
- output action count
- durable World action TTL
- World action maximum attempts

A language frontend cannot bypass ScriptHost or World-side validation.

## API

Create:

```http
POST /v1/scripts
Authorization: Bearer <session>
Content-Type: application/json
```

```json
{
  "object_id": "region-id/42",
  "language": "ogl",
  "source": "..."
}
```

`language` may be `legacy`, `lsl` or `ogl`; omitted values default to `legacy`.

Capability/status metadata:

```text
GET /v1/scripts/capabilities
```

The response contains the OGL command catalog, LSL function catalog, LSL event catalog and counts/percentages generated from the same runtime data used by the engine.

## Status vocabulary

- `implemented` — executable semantics are implemented for the documented 9.0 contract
- `partial` — executable, but intentionally not yet fully compatible with upstream LSL semantics
- `recognized` — catalog/parser awareness exists but executable semantic support is not yet implemented
- `unsupported` — deliberately unavailable/deprecated/unsafe in the current engine

Catalog coverage and semantic implementation are different metrics.
