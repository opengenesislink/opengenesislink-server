# Wiki Handover — OpenGenesisLINK Server 9.0.0-dev

## Milestone identity

OpenGenesisLINK Server 9.0.0-dev is the multi-language ScriptEngine milestone.

- repository: `opengenesislink/opengenesislink-server`
- pull request: #14
- final tested PR head: `6553390214a7aa27f233c878cb8c48b40d5c4a95`
- squash merge: `531487f206c9df2657d359e2a47f2c54e82e72ec`
- version: `9.0.0-dev`
- implementation language: C++23
- license: MPL 2.0
- required targets: Linux x86_64, Linux ARM64/aarch64 and Windows x86_64

This document is intended as a single ingestion source for the OpenGenesisLINK Wiki. It summarizes the architecture and embeds the complete 9.0 OGL/LSL command-status matrix.

## CI evidence

The exact final PR head `6553390214a7aa27f233c878cb8c48b40d5c4a95` passed every required gate before merge.

### Linux C++ CI

Workflow run #177 — run ID `35446393858`.

Ubuntu 24.04 x86_64:

- warnings-as-errors configure: PASS
- full build: PASS
- all unit tests: PASS
- integrated World/Social/Handoff smoke: PASS
- Script World query/ACK smoke: PASS
- OGL/LSL ScriptEngine end-to-end smoke: PASS
- Crossing v3 reserve/rollback smoke: PASS
- Object Crossing v2 end-to-end smoke: PASS

Ubuntu 24.04 ARM64/aarch64:

- warnings-as-errors configure: PASS
- full build: PASS
- all unit tests: PASS
- integrated World/Social/Handoff smoke: PASS
- Script World query/ACK smoke: PASS
- OGL/LSL ScriptEngine end-to-end smoke: PASS
- Crossing v3 reserve/rollback smoke: PASS
- Object Crossing v2 end-to-end smoke: PASS

### Windows C++ CI

Workflow run #148 — run ID `35446393840`.

Windows Server 2022 x86_64:

- MSVC environment: PASS
- vcpkg binary cache: PASS
- OpenSSL setup: PASS
- MSVC `/WX` configure: PASS
- full Core/World/test build: PASS
- unit tests: PASS

## ScriptEngine architecture

9.0 replaces the idea of separate language runtimes with one shared server-side ScriptEngine.

```text
Legacy source ----\
LSL source --------> language frontend -> shared Script IR -> bounded VM
OGL source -------/                                      |
                                                         v
                                                  typed host actions
                                                         |
                                      +------------------+------------------+
                                      |                                     |
                                  Core services                      World action queue
                                      |                                     |
                                      v                                     v
                         social / notifications                  owning World Node
```

All languages therefore share:

- instruction budgets
- variable limits
- persistent VM state limits
- output-action limits
- ScriptHost authorization
- durable Script World action queues
- ACK/NACK/retry/expiry
- World-side object ownership and permission validation
- Object Crossing Script rebinding

No language frontend receives direct access to files, processes, databases or arbitrary sockets.

## Persistent languages

Each Script record now stores one of:

- `legacy`
- `lsl`
- `ogl`

The selected language survives Core restart because Script Runtime persistence is now v3.

Older Script Runtime records are loaded as `legacy` for backward compatibility.

## Script states and event parameters

Script IR handlers now contain:

- logical state
- event name
- ordered parameter names
- instructions

Legacy handlers use wildcard state `*`.

LSL and OGL use state-specific handlers.

Event payloads are newline-delimited and mapped onto handler parameter names.

Example LSL:

```lsl
listen(integer channel, string name, key id, string message)
```

Payload:

```text
7
Alice Example
agent-key
Hello
```

creates VM variables:

- `channel = 7`
- `name = Alice Example`
- `id = agent-key`
- `message = Hello`

## Script API

Create Script:

```http
POST /v1/scripts
Authorization: Bearer <session>
Content-Type: application/json
```

Example:

```json
{
  "object_id": "region-id/42",
  "language": "ogl",
  "source": "..."
}
```

Valid languages:

- `legacy`
- `lsl`
- `ogl`

When `language` is omitted, `legacy` remains the compatibility default.

Execute event:

```text
POST /v1/scripts/event
```

The endpoint also accepts an optional bounded `payload`.

Inspect runtime compatibility:

```text
GET /v1/scripts/capabilities
```

The capability response exposes:

- available languages
- OGL feature catalog
- LSL function catalog
- LSL event catalog
- implemented count
- partial count
- recognized count
- unsupported count
- strict implementation percentage
- executable percentage including partial semantics
- catalog coverage

## Native OGL language

OGL is the native OpenGenesisLINK server scripting language.

Example:

```text
@ogl 1

state default
on touch
let count = 1
inc count by 2
world.say Hello from OGL
world.move 128 128 25
goto active
end

state active
on timer
world.text Active
end
```

The first OGL language version intentionally maps directly to OpenGenesisLINK concepts instead of reproducing historical simulator quirks.

Implemented OGL groups include:

- states/events
- persistent variables
- integer increments
- state transitions
- timers
- chat listeners
- owner notification
- direct user messaging
- object movement
- rotation
- scale
- velocity
- angular velocity
- physics toggle
- floating text
- local chat
- object query
- Region query
- terrain query
- water-level query
- world-time query
- nearby-avatar query

OGL v1 intentionally leaves structured `if/else`, loops, user-defined functions and the full typed-value language for later milestones.

## LSL compatibility frontend

9.0 introduces a clean-room LSL compatibility frontend. It does not embed an OpenSimulator ScriptEngine.

Supported structural syntax includes:

- `default { ... }`
- named states such as `active { ... }`
- event declarations
- typed event parameter names
- `state active;` transitions
- initialized local declarations
- literal/variable assignment
- assignment from implemented deterministic LSL built-ins

Example:

```lsl
default
{
    state_entry()
    {
        llSetTimerEvent(1.0);
        llOwnerSay("ready");
    }

    listen(integer channel, string name, key id, string message)
    {
        integer length = llStringLength(message);
        string upper = llToUpper(message);
        llSay(0, upper);
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

## Current executable LSL host/world bridge

9.0 provides executable mappings for:

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
- `llResetScript` with current partial reset semantics

They remain marked `partial` whenever OpenGenesisLINK does not yet reproduce the entire Second Life behavioral contract.

## Deterministic LSL built-ins

9.0 also adds executable deterministic built-ins for parts of:

- integer/float math
- trigonometry
- strings
- Base64
- vector math
- SHA-256
- date/time

Examples that now execute through the sandboxed VM:

```lsl
integer n = llAbs(-3);
string upper = llToUpper(message);
float magnitude = llVecMag(<3,4,0>);
string digest = llSHA256String("OpenGenesisLINK");
integer unix_time = llGetUnixTime();
```

## Implementation status

The 9.0 catalog status is deliberately strict.

| Area | Total | Implemented | Partial | Recognized | Unsupported | Strict implementation | Executable incl. partial | Catalog coverage |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| OGL | 31 | 27 | 0 | 0 | 4 | 87.10% | 87.10% | 100.00% |
| LSL functions | 523 | 27 | 24 | 447 | 25 | 5.16% | 9.75% | 100.00% |
| LSL events | 44 | 1 | 3 | 39 | 1 | 2.27% | 9.09% | 100.00% |

Definitions:

- **implemented** — executable semantics implemented for the 9.0 contract
- **partial** — executable but not fully upstream-compatible
- **recognized** — catalog/parser awareness exists but server semantics are not executable yet
- **unsupported** — explicitly unavailable/deprecated/unsafe in the current contract

Catalog coverage must never be presented as semantic compatibility.

## LSL catalog reference note

The 9.0 development reference used the Second Life Wiki LSL function/event categories.

The function category reports more pages than the normalized OpenGenesisLINK runtime catalog because category pages can contain aliases, navigation/category entries and other pages that are not treated as distinct canonical `ll*` built-ins.

OpenGenesisLINK 9.0 tracks 523 normalized canonical `ll*` identifiers.

The event compatibility catalog contains 44 entries, including the documentation-only `event_order` category entry.

## Process-level ScriptEngine test

The Linux ScriptEngine smoke starts a real Core and World Node and then:

1. requests ScriptEngine capability/status metadata
2. creates an authenticated user/session
3. joins a Region through the real Scene endpoint
4. creates a real Scene object
5. creates an OGL Script bound to that object
6. executes OGL state/variable/world commands
7. verifies movement and floating text on the World Node
8. creates an LSL Script bound to the same object
9. executes `state_entry`
10. executes `listen` with event parameters
11. validates deterministic built-ins
12. performs LSL World movement
13. performs an LSL state transition
14. waits for the Core-scheduled timer through a bounded poll
15. verifies timer-driven floating text on the World Node
16. verifies persisted Script languages and logical state

The smoke passed on both Linux x86_64 and ARM64 for the final tested 9.0 PR head.

## Deliberate boundary: “full LSL”

The 9.0 milestone establishes the complete compatibility catalog and the architectural path required to reach broad LSL compatibility, but it does **not** claim 100% Second Life LSL semantic compatibility.

Important remaining areas include:

- full expression grammar and operators
- structured `if/else`
- loops
- global declarations and constants
- user-defined functions
- complete typed values and casting rules
- full list semantics
- JSON fidelity
- rotations/quaternions in expressions
- synchronous read functions
- full object/linkset primitives
- Inventory functions
- Asset/Notecard APIs
- sensors
- collisions
- full touch event source wiring
- permissions/dialog lifecycle
- Experience APIs
- HTTP-out and inbound HTTP events
- DataServer requests
- email APIs
- sound/media APIs
- animation APIs
- vehicle APIs
- camera/control APIs
- money/payment semantics
- attachment APIs
- teleport APIs
- environment APIs
- Linkset Data APIs
- full automatic `state_entry/state_exit` semantics
- event ordering fidelity

Future milestones should move entries from `recognized` to `partial`, then from `partial` to `implemented`, with a regression test for every promotion.

## Authoritative 9.0 Wiki files

The Wiki should treat these files as authoritative for the ScriptEngine milestone:

- `docs/SCRIPT-ENGINE-v2.md`
- `docs/OGL-SCRIPT-v1.md`
- `docs/LSL-COMPAT-v1.md`
- `docs/SCRIPT-COMMAND-STATUS-9.0.md`
- `docs/WIKI-HANDOVER-9.0.0-dev.md`

The complete per-command matrix follows below so this handover can also be consumed as a single standalone Wiki source.

---

# OpenGenesisLINK 9.0 — Script Command Status

Diese Datei wird aus den in 9.0 gepflegten ScriptEngine-Katalogen abgeleitet. Sie ist die Wiki-Referenz für den Implementierungsstand der nativen OGL-Skriptsprache und der LSL-Kompatibilität.

## Statusdefinition

| Status | Bedeutung |
|---|---|
| ✅ `implemented` | Im 9.0-Vertrag ausführbar und für den dokumentierten Umfang implementiert. |
| 🟡 `partial` | Ausführbar, aber Semantik/Fidelity ist gegenüber vollständigem LSL bewusst eingeschränkt. |
| 🔵 `recognized` | Katalog-/Parserwissen vorhanden, aber die eigentliche Server-Semantik ist noch nicht ausführbar. |
| ⛔ `unsupported` | Derzeit absichtlich nicht unterstützt, veraltet oder nicht sicher/geeignet für den aktuellen Engine-Vertrag. |

**Wichtig:** Katalogabdeckung ist nicht dasselbe wie semantische Kompatibilität.

## Zusammenfassung

| Bereich | Gesamt | Implementiert | Partial | Recognized | Unsupported | Strikt implementiert | Ausführbar inkl. Partial | Katalogisiert |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| OGL | 31 | 27 | 0 | 0 | 4 | 87.10% | 87.10% | 100.00% |
| LSL Funktionen | 523 | 27 | 24 | 447 | 25 | 5.16% | 9.75% | 100.00% |
| LSL Events | 44 | 1 | 3 | 39 | 1 | 2.27% | 9.09% | 100.00% |

### Metrik

- **Strikt implementiert** = `implemented / total × 100`.
- **Ausführbar inkl. Partial** = `(implemented + partial) / total × 100`.
- **Katalogisiert** = alle Einträge, unabhängig davon ob sie ausführbar sind.
- Es wird **kein gewichteter Marketing-Prozentsatz** verwendet.

## OGL-Befehle und Sprachmerkmale

| Befehl | Kategorie | Status |
|---|---|---|
| `state` | language | ✅ implemented |
| `on` | language | ✅ implemented |
| `let` | language | ✅ implemented |
| `inc` | language | ✅ implemented |
| `goto` | language | ✅ implemented |
| `emit` | runtime | ✅ implemented |
| `timer.every` | runtime | ✅ implemented |
| `chat.listen` | runtime | ✅ implemented |
| `owner.notify` | social | ✅ implemented |
| `user.message` | social | ✅ implemented |
| `world.move` | world | ✅ implemented |
| `world.rotate` | world | ✅ implemented |
| `world.scale` | world | ✅ implemented |
| `world.velocity` | world | ✅ implemented |
| `world.angular_velocity` | world | ✅ implemented |
| `world.physics` | world | ✅ implemented |
| `world.text` | world | ✅ implemented |
| `world.say` | world | ✅ implemented |
| `world.whisper` | world | ✅ implemented |
| `world.shout` | world | ✅ implemented |
| `world.object` | query | ✅ implemented |
| `world.region` | query | ✅ implemented |
| `world.terrain` | query | ✅ implemented |
| `world.water` | query | ✅ implemented |
| `world.time` | query | ✅ implemented |
| `world.nearby` | query | ✅ implemented |
| `stop` | runtime | ✅ implemented |
| `if/else` | language | ⛔ unsupported |
| `while/for` | language | ⛔ unsupported |
| `functions` | language | ⛔ unsupported |
| `typed values` | language | ⛔ unsupported |


## LSL Events

| Befehl | Kategorie | Status |
|---|---|---|
| `attach` | event | 🔵 recognized |
| `at_rot_target` | event | 🔵 recognized |
| `at_target` | event | 🔵 recognized |
| `changed` | event | 🔵 recognized |
| `collision` | event | 🔵 recognized |
| `collision_end` | event | 🔵 recognized |
| `collision_start` | event | 🔵 recognized |
| `control` | event | 🔵 recognized |
| `dataserver` | event | 🔵 recognized |
| `email` | event | 🔵 recognized |
| `event_order` | documentation | ⛔ unsupported |
| `experience_permissions` | event | 🔵 recognized |
| `experience_permissions_denied` | event | 🔵 recognized |
| `final_damage` | event | 🔵 recognized |
| `game_control` | event | 🔵 recognized |
| `http_request` | event | 🔵 recognized |
| `http_response` | event | 🔵 recognized |
| `land_collision` | event | 🔵 recognized |
| `land_collision_end` | event | 🔵 recognized |
| `land_collision_start` | event | 🔵 recognized |
| `linkset_data` | event | 🔵 recognized |
| `link_message` | event | 🔵 recognized |
| `listen` | event | 🟡 partial |
| `money` | event | 🔵 recognized |
| `moving_end` | event | 🔵 recognized |
| `moving_start` | event | 🔵 recognized |
| `not_at_rot_target` | event | 🔵 recognized |
| `not_at_target` | event | 🔵 recognized |
| `no_sensor` | event | 🔵 recognized |
| `object_rez` | event | 🔵 recognized |
| `on_damage` | event | 🔵 recognized |
| `on_death` | event | 🔵 recognized |
| `on_rez` | event | 🔵 recognized |
| `path_update` | event | 🔵 recognized |
| `remote_data` | event | 🔵 recognized |
| `run_time_permissions` | event | 🔵 recognized |
| `sensor` | event | 🔵 recognized |
| `state_entry` | event | 🟡 partial |
| `state_exit` | event | 🔵 recognized |
| `timer` | event | ✅ implemented |
| `touch` | event | 🔵 recognized |
| `touch_end` | event | 🔵 recognized |
| `touch_start` | event | 🟡 partial |
| `transaction_result` | event | 🔵 recognized |


## LSL Funktionen — vollständiger OpenGenesisLINK-Katalog

Der OpenGenesisLINK-Katalog enthält 523 normalisierte kanonische `ll*`-Identifier. Die für 9.0 herangezogene offizielle Second-Life-Wiki-Kategorie meldet 534 Seiten. Diese beiden Zahlen werden nicht gleichgesetzt; die Differenz wird nicht als zusätzliche implementierte Funktion interpretiert.

Referenz:
- https://wiki.secondlife.com/wiki/Category:LSL_Functions
- https://wiki.secondlife.com/wiki/Category:LSL_Events

| Befehl | Status |
|---|---|
| `llAbs` | ✅ implemented |
| `llAcos` | ✅ implemented |
| `llAddToLandBanList` | 🔵 recognized |
| `llAddToLandPassList` | 🔵 recognized |
| `llAdjustDamage` | 🔵 recognized |
| `llAdjustSoundVolume` | 🔵 recognized |
| `llAgentInExperience` | 🔵 recognized |
| `llAllowInventoryDrop` | 🔵 recognized |
| `llAngleBetween` | 🔵 recognized |
| `llApplyImpulse` | 🔵 recognized |
| `llApplyRotationalImpulse` | 🔵 recognized |
| `llAsin` | ✅ implemented |
| `llAtan2` | ✅ implemented |
| `llAttachToAvatar` | 🔵 recognized |
| `llAttachToAvatarTemp` | 🔵 recognized |
| `llAvatarOnLinkSitTarget` | 🔵 recognized |
| `llAvatarOnSitTarget` | 🔵 recognized |
| `llAxes2Rot` | 🔵 recognized |
| `llAxisAngle2Rot` | 🔵 recognized |
| `llBase64ToInteger` | ✅ implemented |
| `llBase64ToString` | ✅ implemented |
| `llBreakAllLinks` | 🔵 recognized |
| `llBreakLink` | 🔵 recognized |
| `llCastRay` | 🔵 recognized |
| `llCeil` | ✅ implemented |
| `llChar` | 🟡 partial |
| `llClearCameraParams` | 🔵 recognized |
| `llClearLinkMedia` | 🔵 recognized |
| `llClearPrimMedia` | 🔵 recognized |
| `llCloseRemoteDataChannel` | ⛔ unsupported |
| `llCloud` | ⛔ unsupported |
| `llCollisionFilter` | 🔵 recognized |
| `llCollisionSound` | 🔵 recognized |
| `llCollisionSprite` | 🔵 recognized |
| `llComputeHash` | 🔵 recognized |
| `llCos` | ✅ implemented |
| `llCreateCharacter` | 🔵 recognized |
| `llCreateKeyValue` | 🔵 recognized |
| `llCreateLink` | 🔵 recognized |
| `llCSV2List` | 🔵 recognized |
| `llDamage` | 🔵 recognized |
| `llDataSizeKeyValue` | 🔵 recognized |
| `llDeleteCharacter` | 🔵 recognized |
| `llDeleteKeyValue` | 🔵 recognized |
| `llDeleteSubList` | 🔵 recognized |
| `llDeleteSubString` | 🟡 partial |
| `llDerezObject` | 🔵 recognized |
| `llDetachFromAvatar` | 🔵 recognized |
| `llDetectedDamage` | 🔵 recognized |
| `llDetectedGrab` | 🔵 recognized |
| `llDetectedGroup` | 🔵 recognized |
| `llDetectedKey` | 🔵 recognized |
| `llDetectedLinkNumber` | 🔵 recognized |
| `llDetectedName` | 🔵 recognized |
| `llDetectedOwner` | 🔵 recognized |
| `llDetectedPos` | 🔵 recognized |
| `llDetectedRezzer` | 🔵 recognized |
| `llDetectedRot` | 🔵 recognized |
| `llDetectedTouchBinormal` | 🔵 recognized |
| `llDetectedTouchFace` | 🔵 recognized |
| `llDetectedTouchNormal` | 🔵 recognized |
| `llDetectedTouchPos` | 🔵 recognized |
| `llDetectedTouchST` | 🔵 recognized |
| `llDetectedTouchUV` | 🔵 recognized |
| `llDetectedType` | 🔵 recognized |
| `llDetectedVel` | 🔵 recognized |
| `llDialog` | 🔵 recognized |
| `llDie` | 🔵 recognized |
| `llDumpList2String` | 🔵 recognized |
| `llEdgeOfWorld` | 🔵 recognized |
| `llEjectFromLand` | 🔵 recognized |
| `llEmail` | 🔵 recognized |
| `llEscapeURL` | 🔵 recognized |
| `llEuler2Rot` | 🔵 recognized |
| `llEvade` | 🔵 recognized |
| `llExecCharacterCmd` | 🔵 recognized |
| `llFabs` | ✅ implemented |
| `llFindNotecardTextSync` | 🔵 recognized |
| `llFleeFrom` | 🔵 recognized |
| `llFloor` | ✅ implemented |
| `llForceMouselook` | 🔵 recognized |
| `llFrand` | 🔵 recognized |
| `llGenerateKey` | 🔵 recognized |
| `llGetAccel` | 🔵 recognized |
| `llGetAgentInfo` | 🔵 recognized |
| `llGetAgentLanguage` | 🔵 recognized |
| `llGetAgentList` | 🔵 recognized |
| `llGetAgentSize` | 🔵 recognized |
| `llGetAlpha` | 🔵 recognized |
| `llGetAndResetTime` | 🔵 recognized |
| `llGetAnimation` | 🔵 recognized |
| `llGetAnimationList` | 🔵 recognized |
| `llGetAnimationOverride` | 🔵 recognized |
| `llGetAttached` | 🔵 recognized |
| `llGetAttachedList` | 🔵 recognized |
| `llGetAttachedListFiltered` | 🔵 recognized |
| `llGetBoundingBox` | 🔵 recognized |
| `llGetCameraAspect` | 🔵 recognized |
| `llGetCameraFOV` | 🔵 recognized |
| `llGetCameraPos` | 🔵 recognized |
| `llGetCameraRot` | 🔵 recognized |
| `llGetCenterOfMass` | 🔵 recognized |
| `llGetClosestNavPoint` | 🔵 recognized |
| `llGetColor` | 🔵 recognized |
| `llGetCreator` | 🔵 recognized |
| `llGetDate` | ✅ implemented |
| `llGetDayLength` | 🔵 recognized |
| `llGetDayOffset` | 🔵 recognized |
| `llGetDisplayName` | 🔵 recognized |
| `llGetEnergy` | 🔵 recognized |
| `llGetEnv` | 🔵 recognized |
| `llGetEnvironment` | 🔵 recognized |
| `llGetExperienceDetails` | 🔵 recognized |
| `llGetExperienceErrorMessage` | 🔵 recognized |
| `llGetForce` | 🔵 recognized |
| `llGetFreeMemory` | 🔵 recognized |
| `llGetFreeURLs` | 🔵 recognized |
| `llGetGameControlMode` | 🔵 recognized |
| `llGetGameControlModeAxes` | 🔵 recognized |
| `llGetGameControlModeButtons` | 🔵 recognized |
| `llGetGeometricCenter` | 🔵 recognized |
| `llGetGMTclock` | 🔵 recognized |
| `llGetHealth` | 🔵 recognized |
| `llGetHTTPHeader` | 🔵 recognized |
| `llGetInventoryAcquireTime` | 🔵 recognized |
| `llGetInventoryCreator` | 🔵 recognized |
| `llGetInventoryDesc` | 🔵 recognized |
| `llGetInventoryKey` | 🔵 recognized |
| `llGetInventoryName` | 🔵 recognized |
| `llGetInventoryNumber` | 🔵 recognized |
| `llGetInventoryPermMask` | 🔵 recognized |
| `llGetInventoryType` | 🔵 recognized |
| `llGetKey` | 🔵 recognized |
| `llGetLandOwnerAt` | 🔵 recognized |
| `llGetLinkKey` | 🔵 recognized |
| `llGetLinkMedia` | 🔵 recognized |
| `llGetLinkName` | 🔵 recognized |
| `llGetLinkNumber` | 🔵 recognized |
| `llGetLinkNumberOfSides` | 🔵 recognized |
| `llGetLinkPrimitiveParams` | 🔵 recognized |
| `llGetLinkSitFlags` | 🔵 recognized |
| `llGetListEntryType` | 🔵 recognized |
| `llGetListLength` | 🔵 recognized |
| `llGetLocalPos` | 🔵 recognized |
| `llGetLocalRot` | 🔵 recognized |
| `llGetMass` | 🔵 recognized |
| `llGetMassMKS` | 🔵 recognized |
| `llGetMaxScaleFactor` | 🔵 recognized |
| `llGetMemoryLimit` | 🔵 recognized |
| `llGetMinScaleFactor` | 🔵 recognized |
| `llGetMoonDirection` | 🔵 recognized |
| `llGetMoonRotation` | 🔵 recognized |
| `llGetNextEmail` | 🔵 recognized |
| `llGetNotecardLine` | 🔵 recognized |
| `llGetNotecardLineSync` | 🔵 recognized |
| `llGetNumberOfNotecardLines` | 🔵 recognized |
| `llGetNumberOfPrims` | 🔵 recognized |
| `llGetNumberOfSides` | 🔵 recognized |
| `llGetObjectAnimationNames` | 🔵 recognized |
| `llGetObjectDesc` | 🔵 recognized |
| `llGetObjectDetails` | 🔵 recognized |
| `llGetObjectLinkKey` | 🔵 recognized |
| `llGetObjectMass` | 🔵 recognized |
| `llGetObjectName` | 🔵 recognized |
| `llGetObjectPermMask` | 🔵 recognized |
| `llGetObjectPrimCount` | 🔵 recognized |
| `llGetOmega` | 🔵 recognized |
| `llGetOwner` | 🔵 recognized |
| `llGetOwnerKey` | 🔵 recognized |
| `llGetParcelDetails` | 🔵 recognized |
| `llGetParcelFlags` | 🔵 recognized |
| `llGetParcelMaxPrims` | 🔵 recognized |
| `llGetParcelMusicURL` | 🔵 recognized |
| `llGetParcelPrimCount` | 🔵 recognized |
| `llGetParcelPrimOwners` | 🔵 recognized |
| `llGetPermissions` | 🔵 recognized |
| `llGetPermissionsKey` | 🔵 recognized |
| `llGetPhysicsMaterial` | 🔵 recognized |
| `llGetPos` | 🔵 recognized |
| `llGetPrimitiveParams` | 🔵 recognized |
| `llGetPrimMediaParams` | 🔵 recognized |
| `llGetRegionAgentCount` | 🔵 recognized |
| `llGetRegionCorner` | 🔵 recognized |
| `llGetRegionDayLength` | 🔵 recognized |
| `llGetRegionDayOffset` | 🔵 recognized |
| `llGetRegionFlags` | 🔵 recognized |
| `llGetRegionFPS` | 🔵 recognized |
| `llGetRegionMoonDirection` | 🔵 recognized |
| `llGetRegionMoonRotation` | 🔵 recognized |
| `llGetRegionName` | 🔵 recognized |
| `llGetRegionSunDirection` | 🔵 recognized |
| `llGetRegionSunRotation` | 🔵 recognized |
| `llGetRegionTimeDilation` | 🔵 recognized |
| `llGetRegionTimeOfDay` | 🔵 recognized |
| `llGetRenderMaterial` | 🔵 recognized |
| `llGetRootPosition` | 🔵 recognized |
| `llGetRootRotation` | 🔵 recognized |
| `llGetRot` | 🔵 recognized |
| `llGetScale` | 🔵 recognized |
| `llGetScriptName` | 🔵 recognized |
| `llGetScriptState` | 🔵 recognized |
| `llGetSimStats` | 🔵 recognized |
| `llGetSimulatorHostname` | 🔵 recognized |
| `llGetSPMaxMemory` | 🔵 recognized |
| `llGetStartParameter` | 🔵 recognized |
| `llGetStartString` | 🔵 recognized |
| `llGetStaticPath` | 🔵 recognized |
| `llGetStatus` | 🔵 recognized |
| `llGetSubString` | 🟡 partial |
| `llGetSunDirection` | 🔵 recognized |
| `llGetSunRotation` | 🔵 recognized |
| `llGetTexture` | 🔵 recognized |
| `llGetTextureOffset` | 🔵 recognized |
| `llGetTextureRot` | 🔵 recognized |
| `llGetTextureScale` | 🔵 recognized |
| `llGetTime` | 🔵 recognized |
| `llGetTimeOfDay` | 🔵 recognized |
| `llGetTimestamp` | ✅ implemented |
| `llGetTorque` | 🔵 recognized |
| `llGetUnixTime` | ✅ implemented |
| `llGetUsedMemory` | 🔵 recognized |
| `llGetUsername` | 🔵 recognized |
| `llGetVel` | 🔵 recognized |
| `llGetVisualParams` | 🔵 recognized |
| `llGetWallclock` | 🔵 recognized |
| `llGiveAgentInventory` | 🔵 recognized |
| `llGiveInventory` | 🔵 recognized |
| `llGiveInventoryList` | 🔵 recognized |
| `llGiveMoney` | 🔵 recognized |
| `llGodLikeRezObject` | ⛔ unsupported |
| `llGround` | 🔵 recognized |
| `llGroundContour` | 🔵 recognized |
| `llGroundNormal` | 🔵 recognized |
| `llGroundRepel` | 🔵 recognized |
| `llGroundSlope` | 🔵 recognized |
| `llHash` | 🔵 recognized |
| `llHMAC` | 🔵 recognized |
| `llHTTPRequest` | 🔵 recognized |
| `llHTTPResponse` | 🔵 recognized |
| `llInsertString` | 🟡 partial |
| `llInstantMessage` | 🟡 partial |
| `llIntegerToBase64` | ✅ implemented |
| `llIsFriend` | 🔵 recognized |
| `llIsLinkGLTFMaterial` | 🔵 recognized |
| `llJson2List` | 🔵 recognized |
| `llJsonGetValue` | 🔵 recognized |
| `llJsonSetValue` | 🔵 recognized |
| `llJsonValueType` | 🔵 recognized |
| `llKey2Name` | 🔵 recognized |
| `llKeyCountKeyValue` | 🔵 recognized |
| `llKeysKeyValue` | 🔵 recognized |
| `llLinear2sRGB` | 🔵 recognized |
| `llLinkAdjustSoundVolume` | 🔵 recognized |
| `llLinkParticleSystem` | 🔵 recognized |
| `llLinkPlaySound` | 🔵 recognized |
| `llLinksetDataAvailable` | 🔵 recognized |
| `llLinksetDataCountFound` | 🔵 recognized |
| `llLinksetDataCountKeys` | 🔵 recognized |
| `llLinksetDataDelete` | 🔵 recognized |
| `llLinksetDataDeleteFound` | 🔵 recognized |
| `llLinksetDataDeleteProtected` | 🔵 recognized |
| `llLinksetDataFindKeys` | 🔵 recognized |
| `llLinksetDataListKeys` | 🔵 recognized |
| `llLinksetDataRead` | 🔵 recognized |
| `llLinksetDataReadProtected` | 🔵 recognized |
| `llLinksetDataReset` | 🔵 recognized |
| `llLinksetDataWrite` | 🔵 recognized |
| `llLinksetDataWriteProtected` | 🔵 recognized |
| `llLinkSetSoundQueueing` | 🔵 recognized |
| `llLinkSetSoundRadius` | 🔵 recognized |
| `llLinkSitTarget` | 🔵 recognized |
| `llLinkStopSound` | 🔵 recognized |
| `llList2CSV` | 🔵 recognized |
| `llList2Float` | 🔵 recognized |
| `llList2Integer` | 🔵 recognized |
| `llList2Json` | 🔵 recognized |
| `llList2Key` | 🔵 recognized |
| `llList2List` | 🔵 recognized |
| `llList2ListSlice` | 🔵 recognized |
| `llList2ListStrided` | 🔵 recognized |
| `llList2Rot` | 🔵 recognized |
| `llList2String` | 🔵 recognized |
| `llList2Vector` | 🔵 recognized |
| `llListen` | 🟡 partial |
| `llListenControl` | 🔵 recognized |
| `llListenRemove` | 🔵 recognized |
| `llListFindList` | 🔵 recognized |
| `llListFindListNext` | 🔵 recognized |
| `llListFindStrided` | 🔵 recognized |
| `llListInsertList` | 🔵 recognized |
| `llListRandomize` | 🔵 recognized |
| `llListReplaceList` | 🔵 recognized |
| `llListSort` | 🔵 recognized |
| `llListSortStrided` | 🔵 recognized |
| `llListStatistics` | 🔵 recognized |
| `llLoadURL` | 🔵 recognized |
| `llLog` | 🟡 partial |
| `llLog10` | 🟡 partial |
| `llLookAt` | 🔵 recognized |
| `llLoopSound` | 🔵 recognized |
| `llLoopSoundMaster` | 🔵 recognized |
| `llLoopSoundSlave` | 🔵 recognized |
| `llMakeExplosion` | ⛔ unsupported |
| `llMakeFire` | ⛔ unsupported |
| `llMakeFountain` | ⛔ unsupported |
| `llMakeSmoke` | ⛔ unsupported |
| `llManageEstateAccess` | 🔵 recognized |
| `llMapBeacon` | 🔵 recognized |
| `llMapDestination` | 🔵 recognized |
| `llMD5String` | 🔵 recognized |
| `llMessageLinked` | 🔵 recognized |
| `llMinEventDelay` | 🔵 recognized |
| `llModifyLand` | 🔵 recognized |
| `llModPow` | 🔵 recognized |
| `llMoveToTarget` | 🔵 recognized |
| `llName2Key` | 🔵 recognized |
| `llNavigateTo` | 🔵 recognized |
| `llOffsetTexture` | 🔵 recognized |
| `llOpenFloater` | ⛔ unsupported |
| `llOpenRemoteDataChannel` | ⛔ unsupported |
| `llOrd` | 🟡 partial |
| `llOverMyLand` | 🔵 recognized |
| `llOwnerSay` | 🟡 partial |
| `llParcelMediaCommandList` | 🔵 recognized |
| `llParcelMediaQuery` | 🔵 recognized |
| `llParseString2List` | 🔵 recognized |
| `llParseStringKeepNulls` | 🔵 recognized |
| `llParticleSystem` | 🔵 recognized |
| `llPassCollisions` | 🔵 recognized |
| `llPassTouches` | 🔵 recognized |
| `llPatrolPoints` | 🔵 recognized |
| `llPlaySound` | 🔵 recognized |
| `llPlaySoundSlave` | 🔵 recognized |
| `llPointAt` | ⛔ unsupported |
| `llPow` | ✅ implemented |
| `llPreloadSound` | 🔵 recognized |
| `llPursue` | 🔵 recognized |
| `llPushObject` | 🔵 recognized |
| `llReadKeyValue` | 🔵 recognized |
| `llRefreshPrimURL` | ⛔ unsupported |
| `llRegionSay` | 🔵 recognized |
| `llRegionSayTo` | 🔵 recognized |
| `llReleaseCamera` | ⛔ unsupported |
| `llReleaseControls` | 🔵 recognized |
| `llReleaseURL` | 🔵 recognized |
| `llRemoteDataReply` | ⛔ unsupported |
| `llRemoteDataSetRegion` | ⛔ unsupported |
| `llRemoteLoadScript` | ⛔ unsupported |
| `llRemoteLoadScriptPin` | 🔵 recognized |
| `llRemoveFromLandBanList` | 🔵 recognized |
| `llRemoveFromLandPassList` | 🔵 recognized |
| `llRemoveInventory` | 🔵 recognized |
| `llRemoveVehicleFlags` | 🔵 recognized |
| `llReplaceAgentEnvironment` | 🔵 recognized |
| `llReplaceEnvironment` | 🔵 recognized |
| `llReplaceSubString` | 🔵 recognized |
| `llRequestAgentData` | 🔵 recognized |
| `llRequestDisplayName` | 🔵 recognized |
| `llRequestExperiencePermissions` | 🔵 recognized |
| `llRequestInventoryData` | 🔵 recognized |
| `llRequestPermissions` | 🔵 recognized |
| `llRequestSecureURL` | 🔵 recognized |
| `llRequestSimulatorData` | 🔵 recognized |
| `llRequestURL` | 🔵 recognized |
| `llRequestUserKey` | 🔵 recognized |
| `llRequestUsername` | 🔵 recognized |
| `llResetAnimationOverride` | 🔵 recognized |
| `llResetLandBanList` | 🔵 recognized |
| `llResetLandPassList` | 🔵 recognized |
| `llResetOtherScript` | 🔵 recognized |
| `llResetScript` | 🟡 partial |
| `llResetTime` | 🔵 recognized |
| `llReturnObjectsByID` | 🔵 recognized |
| `llReturnObjectsByOwner` | 🔵 recognized |
| `llRezAtRoot` | 🔵 recognized |
| `llRezObject` | 🔵 recognized |
| `llRezObjectWithParams` | 🔵 recognized |
| `llRot2Angle` | 🔵 recognized |
| `llRot2Axis` | 🔵 recognized |
| `llRot2Euler` | 🔵 recognized |
| `llRot2Fwd` | 🔵 recognized |
| `llRot2Left` | 🔵 recognized |
| `llRot2Up` | 🔵 recognized |
| `llRotateTexture` | 🔵 recognized |
| `llRotBetween` | 🔵 recognized |
| `llRotLookAt` | 🔵 recognized |
| `llRotTarget` | 🔵 recognized |
| `llRotTargetRemove` | 🔵 recognized |
| `llRound` | ✅ implemented |
| `llSameGroup` | 🔵 recognized |
| `llSay` | 🟡 partial |
| `llScaleByFactor` | 🔵 recognized |
| `llScaleTexture` | 🔵 recognized |
| `llScriptDanger` | 🔵 recognized |
| `llScriptProfiler` | 🔵 recognized |
| `llSendRemoteData` | ⛔ unsupported |
| `llSensor` | 🔵 recognized |
| `llSensorRemove` | 🔵 recognized |
| `llSensorRepeat` | 🔵 recognized |
| `llSetAgentEnvironment` | 🔵 recognized |
| `llSetAgentRot` | 🔵 recognized |
| `llSetAlpha` | 🔵 recognized |
| `llSetAngularVelocity` | 🟡 partial |
| `llSetAnimationOverride` | 🔵 recognized |
| `llSetBuoyancy` | 🔵 recognized |
| `llSetCameraAtOffset` | 🔵 recognized |
| `llSetCameraEyeOffset` | 🔵 recognized |
| `llSetCameraParams` | 🔵 recognized |
| `llSetClickAction` | 🔵 recognized |
| `llSetColor` | 🔵 recognized |
| `llSetContentType` | 🔵 recognized |
| `llSetDamage` | 🔵 recognized |
| `llSetEnvironment` | 🔵 recognized |
| `llSetForce` | 🔵 recognized |
| `llSetForceAndTorque` | 🔵 recognized |
| `llSetLinkGLTFOverrides` | 🔵 recognized |
| `llSetGroundTexture` | 🔵 recognized |
| `llSetHoverHeight` | 🔵 recognized |
| `llSetInventoryPermMask` | ⛔ unsupported |
| `llSetKeyframedMotion` | 🔵 recognized |
| `llSetLinkAlpha` | 🔵 recognized |
| `llSetLinkCamera` | 🔵 recognized |
| `llSetLinkColor` | 🔵 recognized |
| `llSetLinkMedia` | 🔵 recognized |
| `llSetLinkPrimitiveParams` | 🔵 recognized |
| `llSetLinkPrimitiveParamsFast` | 🔵 recognized |
| `llSetLinkRenderMaterial` | 🔵 recognized |
| `llSetLinkSitFlags` | 🔵 recognized |
| `llSetLinkTexture` | 🔵 recognized |
| `llSetLinkTextureAnim` | 🔵 recognized |
| `llSetLocalRot` | 🔵 recognized |
| `llSetMemoryLimit` | 🔵 recognized |
| `llSetObjectDesc` | 🔵 recognized |
| `llSetObjectName` | 🔵 recognized |
| `llSetObjectPermMask` | ⛔ unsupported |
| `llSetParcelMusicURL` | 🔵 recognized |
| `llSetPayPrice` | 🔵 recognized |
| `llSetPhysicsMaterial` | 🔵 recognized |
| `llSetPos` | 🟡 partial |
| `llSetPrimitiveParams` | 🔵 recognized |
| `llSetPrimMediaParams` | 🔵 recognized |
| `llSetPrimURL` | ⛔ unsupported |
| `llSetRegionPos` | 🟡 partial |
| `llSetRemoteScriptAccessPin` | 🔵 recognized |
| `llSetRenderMaterial` | 🔵 recognized |
| `llSetRot` | 🔵 recognized |
| `llSetScale` | 🟡 partial |
| `llSetScriptState` | 🔵 recognized |
| `llSetSitText` | 🔵 recognized |
| `llSetSoundQueueing` | 🔵 recognized |
| `llSetSoundRadius` | 🔵 recognized |
| `llSetStatus` | 🟡 partial |
| `llSetText` | 🟡 partial |
| `llSetTexture` | 🔵 recognized |
| `llSetTextureAnim` | 🔵 recognized |
| `llSetTimerEvent` | 🟡 partial |
| `llSetTorque` | 🔵 recognized |
| `llSetTouchText` | 🔵 recognized |
| `llSetVehicleFlags` | 🔵 recognized |
| `llSetVehicleFloatParam` | 🔵 recognized |
| `llSetVehicleRotationParam` | 🔵 recognized |
| `llSetVehicleType` | 🔵 recognized |
| `llSetVehicleVectorParam` | 🔵 recognized |
| `llSetVelocity` | 🟡 partial |
| `llSHA1String` | 🔵 recognized |
| `llSHA256String` | ✅ implemented |
| `llShout` | 🟡 partial |
| `llSignRSA` | 🔵 recognized |
| `llSin` | ✅ implemented |
| `llSitOnLink` | 🔵 recognized |
| `llSitTarget` | 🔵 recognized |
| `llSleep` | 🔵 recognized |
| `llSound` | ⛔ unsupported |
| `llSoundPreload` | ⛔ unsupported |
| `llSqrt` | 🟡 partial |
| `llsRGB2Linear` | 🔵 recognized |
| `llStartAnimation` | 🔵 recognized |
| `llStartObjectAnimation` | 🔵 recognized |
| `llStopAnimation` | 🔵 recognized |
| `llStopObjectAnimation` | 🔵 recognized |
| `llStopHover` | 🔵 recognized |
| `llStopLookAt` | 🔵 recognized |
| `llStopMoveToTarget` | 🔵 recognized |
| `llStopPointAt` | ⛔ unsupported |
| `llStopSound` | 🔵 recognized |
| `llStringLength` | ✅ implemented |
| `llStringToBase64` | ✅ implemented |
| `llStringTrim` | 🟡 partial |
| `llSubStringIndex` | ✅ implemented |
| `llTakeCamera` | ⛔ unsupported |
| `llTakeControls` | 🔵 recognized |
| `llTan` | ✅ implemented |
| `llTarget` | 🔵 recognized |
| `llTargetedEmail` | 🔵 recognized |
| `llTargetOmega` | 🔵 recognized |
| `llTargetRemove` | 🔵 recognized |
| `llTeleportAgent` | 🔵 recognized |
| `llTeleportAgentGlobalCoords` | 🔵 recognized |
| `llTeleportAgentHome` | 🔵 recognized |
| `llTextBox` | 🔵 recognized |
| `llToLower` | ✅ implemented |
| `llToUpper` | ✅ implemented |
| `llTransferLindenDollars` | 🔵 recognized |
| `llTransferOwnership` | 🔵 recognized |
| `llTriggerSound` | 🔵 recognized |
| `llTriggerSoundLimited` | 🔵 recognized |
| `llUnescapeURL` | 🔵 recognized |
| `llUnSit` | 🔵 recognized |
| `llUpdateCharacter` | 🔵 recognized |
| `llUpdateKeyValue` | 🔵 recognized |
| `llVecDist` | ✅ implemented |
| `llVecMag` | ✅ implemented |
| `llVecNorm` | ✅ implemented |
| `llVerifyRSA` | 🔵 recognized |
| `llVolumeDetect` | 🔵 recognized |
| `llWanderWithin` | 🔵 recognized |
| `llWater` | 🔵 recognized |
| `llWhisper` | 🟡 partial |
| `llWind` | 🔵 recognized |
| `llWorldPosToHUD` | 🔵 recognized |
| `llXorBase64` | 🔵 recognized |
| `llXorBase64Strings` | ⛔ unsupported |
| `llXorBase64StringsCorrect` | ⛔ unsupported |


## 9.0 Grenzen

Die obige Tabelle ist absichtlich streng. Ein 🔵-Eintrag bedeutet nicht, dass ein gleichnamiger Funktionsaufruf stillschweigend akzeptiert wird. Nicht implementierte Funktionen sollen beim Kompilieren/Ausführen einen expliziten Fehler liefern, statt falsche Second-Life-Semantik vorzutäuschen.

Die nächsten LSL-Schwerpunkte sind Ausdrucks-/Control-Flow-Vollständigkeit, globale Variablen und User-Funktionen, synchrone World-Reads, Inventory/Asset/Linkset-Funktionen, Sensor/Collision/Touch-Eventquellen, HTTP/DataServer sowie Permission/Experience-Workflows.

