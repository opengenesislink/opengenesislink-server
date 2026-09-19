# LSL Compatibility v1

OpenGenesisLINK 9.0.0-dev adds an LSL compatibility frontend to the shared OGL ScriptEngine.

This is a clean-room compatibility layer. It does not embed or copy an OpenSimulator ScriptEngine.

## Source form

The frontend understands real LSL state declarations:

```lsl
default
{
    state_entry()
    {
        llOwnerSay("ready");
        llSetTimerEvent(1.0);
    }

    touch_start(integer count)
    {
        llSetPos(<128,128,25>);
        state active;
    }
}

active
{
    timer()
    {
        llSetText("active", <1,1,1>, 1.0);
    }
}
```

Named states are declared as `active { ... }`; `state active;` is the in-handler state transition.

## Implemented parser/runtime foundation

9.0 supports:

- `default` and named state blocks
- event declarations with typed parameter declarations
- state-specific handler selection
- state transitions
- initialized local declarations for integer/float/string/key/vector/rotation/list names
- assignment from literals/variables
- assignment from the deterministic LSL builtin set
- event parameter access from builtin calls and supported host calls
- bounded comments/source/handler execution
- persistent language and VM state

## Host/World function compatibility

Executable partial mappings currently include:

- `llSay`
- `llWhisper`
- `llShout`
- `llOwnerSay`
- `llInstantMessage`
- `llSetTimerEvent`
- `llListen`
- `llSetPos`
- `llSetRegionPos`
- `llSetScale`
- `llSetVelocity`
- `llSetAngularVelocity`
- `llSetText`
- `llSetStatus` for `STATUS_PHYSICS`
- `llResetScript` with partial reset semantics

These are marked `partial` where OpenGenesisLINK semantics are intentionally narrower than Second Life.

## Deterministic builtin library

A first executable pure-function set includes math, strings, Base64, vectors, SHA-256 and time/date functions.

The compatibility matrix marks edge-sensitive/Unicode-sensitive implementations as partial rather than overstating fidelity.

## Event semantics

The parser recognizes the current LSL event catalog, but automatic server dispatch is only implemented for a subset.

- `timer` — implemented through Core maintenance scheduling
- `state_entry` — partial; handler executes when dispatched but automatic state-entry dispatch is not yet complete
- `listen` — partial; parameter/runtime path exists but full chat-listener filtering and World→Core chat routing are not complete
- `touch_start` — partial; handler semantics exist but full Scene interaction dispatch is not yet wired
- other events — recognized/cataloged until their native OpenGenesisLINK event source is implemented

## Upstream catalog reference

Reference snapshot:

- https://wiki.secondlife.com/wiki/Category:LSL_Functions
- https://wiki.secondlife.com/wiki/Category:LSL_Events

On the development snapshot used for 9.0, the official function category reports 534 pages. OpenGenesisLINK currently normalizes its runtime catalog to 523 canonical `ll*` function identifiers. These are deliberately reported as different counts; 9.0 does not claim that every upstream category page represents a distinct executable builtin.

The event category reports 44 pages and OpenGenesisLINK mirrors 44 catalog entries, including the documentation-only `event_order` entry.

## Compatibility claim

9.0 provides 100% catalog visibility for the OpenGenesisLINK 523-function compatibility catalog, but not 100% semantic LSL compatibility.

See `SCRIPT-COMMAND-STATUS-9.0.md` for exact counts and every identifier.
