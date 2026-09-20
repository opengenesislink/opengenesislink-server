# Wiki Handover — OpenGenesisLINK Server 11.0.0-dev

## Milestone identity

OpenGenesisLINK Server 11.0.0-dev is the Script Runtime Completion milestone.

- repository: `opengenesislink/opengenesislink-server`
- pull request: #16
- final tested PR head: `d26cbccbdd3f60e259668961d9da2b3427baee80`
- squash merge: `8c94dd35aa25713e46f1ae2eb5dd9a37f0860d48`
- version: `11.0.0-dev`
- implementation language: C++23
- license: MPL 2.0
- required targets: Linux x86_64, Linux ARM64/aarch64 and Windows x86_64

11.0 extends the shared ScriptEngine introduced in 9.0. It does not create a second scripting runtime. Legacy scripts, LSL compatibility scripts and native OGL scripts continue to execute through the same bounded VM, persistence model and ScriptHost policy layer.

## ScriptEngine v3

The shared VM now supports bounded control flow in addition to the existing data, host-action and query instructions.

New IR/runtime operations:

- unconditional jump
- conditional jump
- compile-time jump target validation
- runtime numeric/string comparisons

Supported comparison operators:

- `==`
- `!=`
- `<`
- `<=`
- `>`
- `>=`

Control flow remains sandboxed by the existing per-event instruction budget. A script cannot create an unbounded CPU loop simply by using OGL `while` or `for`; once the instruction budget is exhausted the VM returns `instruction-budget-exceeded`.

## OGL v2

The defined OGL feature matrix is now 31/31 implemented.

New 11.0 language features:

### Conditions

```text
if count == 3
    world.say matched
else
    world.say other
endif
```

### While loops

```text
while count < 10
    inc count by 1
endwhile
```

### For loops

```text
for i from 1 to 5 step 1
    inc total by 1
endfor
```

Negative non-zero steps are accepted for descending loops.

### Functions

OGL v2 supports reusable parameterless procedures that are expanded into the bounded shared IR at compile time.

```text
function bump
    inc count by 1
endfunction

state default
on touch
call bump
end
```

Call expansion is bounded and recursion depth is limited.

### Typed values

OGL `let` supports explicit declarations for:

- integer
- float
- bool
- string
- key
- vector
- rotation
- list

Example:

```text
let integer count = 0
let bool enabled = true
let string message = "hello"
let vector position = <128,128,25>
```

## OGL status

| Area | Total | Implemented | Partial | Recognized | Unsupported |
| --- | ---: | ---: | ---: | ---: | ---: |
| OGL features | 31 | 31 | 0 | 0 | 0 |

Strict OGL implementation percentage for the defined contract: **100.00%**.

The canonical command-by-command matrix is:

`docs/SCRIPT-COMMAND-STATUS-11.0.md`

## LSL compatibility progress

11.0 intentionally distinguishes catalog coverage from executable semantics.

Current 11.0 catalog:

| Area | Total | Implemented | Partial | Recognized | Unsupported |
| --- | ---: | ---: | ---: | ---: | ---: |
| LSL functions | 523 | 56 | 24 | 418 | 25 |
| LSL events | 44 | 1 | 3 | 39 | 1 |

Strict LSL function implementation: **10.71%**.

Executable LSL function coverage including partial implementations: **15.30%**.

The catalog is 100% tracked, but 11.0 does **not** claim complete Second Life LSL semantic compatibility.

## LSL deterministic builtin expansion

11.0 adds sandboxed deterministic implementations in areas that do not require missing external World services.

New groups include:

- list conversion and length
- list indexed extraction
- list slicing/deletion
- list search
- list insertion/replacement
- CSV/list conversion
- URL escape/unescape
- SHA-1 hashing
- generated keys
- Euler to rotation conversion
- axis-angle to rotation
- rotation to angle/axis/euler
- rotation forward/left/up vectors
- rotation between vectors
- angular distance between rotations

Existing math, strings, Base64, SHA-256, vectors and UTC time/date functions remain available.

## Security and sandbox behavior

The new control-flow functionality does not bypass the established VM safety model.

Still enforced:

- instruction budget
- maximum variable count
- maximum serialized state bytes
- maximum output actions per event
- handler size limits
- source size limits
- function expansion limit
- procedure recursion depth limit
- ScriptHost authorization
- World-side owner validation
- durable ACK/NACK/retry/expiry for Script-to-World actions

No script frontend receives arbitrary file, process, database or socket access.

## Persistence and crossing

11.0 keeps the same language-aware persistent Script records introduced earlier.

The following continue to survive Core restart:

- language selection: `legacy`, `lsl`, `ogl`
- logical state
- VM variables
- timers/runtime state according to the existing runtime format

Object Crossing continues to atomically rebind Script bindings only after committed object migration.

## API capabilities

API discovery advertises the new runtime/language levels:

- `script-engine-v3`
- `script-language-ogl-v2`

The existing capability endpoint remains:

`GET /v1/scripts/capabilities`

It returns machine-readable OGL, LSL-function and LSL-event status catalogs with implementation percentages.

## Test coverage

11.0 adds/updates tests for:

- OGL typed values
- OGL functions
- OGL if/else
- OGL while
- OGL for
- shared-VM conditional execution
- expanded LSL list functions
- URL escaping
- SHA-1
- quaternion/rotation helpers
- OGL v2 Core + World process smoke

Before the final documentation commit, head `6d74e79bde8cc64f78860675cd078a94a68f4fce` passed:

- C++ CI Linux x86_64
- C++ CI Linux ARM64/aarch64
- Windows C++ CI x86_64/MSVC
- Database Integration CI

The final release/documentation head must pass the same gates before merge.

## Compatibility

11.0 retains the 10.0 production foundation:

- SQLite
- PostgreSQL
- MariaDB
- migrations
- SQL-authoritative production Core stores
- production secret validation
- storage health/metrics
- Linux x86_64
- Linux ARM64/aarch64
- Windows x86_64/MSVC

It also retains the previously implemented World, Crossing, Physics, Social, Asset/Inventory, OGL-FED and Hypergrid layers.

## Remaining scripting work after 11.0

OGL reaches 100% for the **defined OGL v2 feature contract**, but the overall scripting roadmap is not complete because LSL compatibility is still intentionally incomplete.

The next LSL-heavy work should focus on server-integrated semantics such as:

- detected/touch/collision context
- link messages and linkset data
- inventory/notecard operations
- sensors
- object/link/prim parameter APIs
- permissions and attachment lifecycle
- rezzing/derezzing
- parcel/land functions
- HTTP/dataserver events
- vehicle functions when the physics/runtime layer supports their semantics

These functions require real server/world integrations rather than merely deterministic local builtins.

## Acceptance state

11.0 Script Runtime Completion acceptance targets:

- OGL v2 defined feature matrix: 31/31
- OGL strict implementation: 100.00%
- Shared bounded control flow: implemented
- Typed OGL declarations: implemented
- OGL procedures: implemented
- LSL strict functions: 56/523
- LSL executable functions incl. partial: 80/523
- Machine-readable command status matrix: present
- Linux x86_64 CI: required
- Linux ARM64/aarch64 CI: required
- Windows x86_64 CI: required
- Database Integration CI: PASS
- Linux x86_64 CI: PASS
- Linux ARM64/aarch64 CI: PASS
- Windows x86_64/MSVC CI: PASS
- PR #16 merged: PASS

The canonical 11.0 merge commit is:

`8c94dd35aa25713e46f1ae2eb5dd9a37f0860d48`
