# Wiki Handover — OpenGenesisLINK Server 14.0.0-dev

## Milestone

OpenGenesisLINK Server 14.0.0-dev is the **Federation Completion** milestone.

- repository: `opengenesislink/opengenesislink-server`
- pull request: #19
- final tested PR head: `f1e4f5d2d47d9a0901d8677249e3b9be3367eee1`
- squash merge: `de66c4290fe9ab7dcc3fe06068b96dc6fe1f71ad`
- version: `14.0.0-dev`
- implementation: C++23
- license: MPL 2.0
- native federation: `OGL-FED/2`
- legacy interoperability: separate OpenSimulator Hypergrid bridge

14.0 builds on the 13.0 Viewer/Scene contract and completes the first coherent native cross-Grid travel plus read-service federation path.

## Native federation flow

```text
Home Grid
  ├─ persistent Ed25519 Grid identity
  ├─ trusted peer record
  ├─ short-lived Service Grant
  └─ signed OGL-FED/2 Travel Token
          │
          ▼
Destination Grid
  ├─ verifies issuer, audience, signature and lifetime
  ├─ rejects replay
  ├─ creates Foreign Session
  ├─ applies Region/Estate/Parcel policy
  ├─ issues native Scene Ticket
  └─ may call granted Home Grid services
```

Native federation remains independent from Hypergrid credentials and AgentCircuitData.

## Trust and key rotation

A peer is identified by Grid ID, HTTPS base URL and Ed25519 public key.

14.0 pins the key of an active trusted peer. Replacing that key through a normal trust update is rejected with:

`federation-peer-key-change-requires-revocation`

A legitimate rotation therefore requires explicit revocation followed by re-trust with the replacement key.

Revoking a peer also:

- invalidates outstanding outbound grants for that audience
- logs out active inbound native Foreign Sessions from that issuer
- blocks future native travel involving that peer

## OGL-FED/2 Travel Token

New native travel issuance uses OGL-FED/2.

The signed claims contain:

- issuer Grid
- audience Grid
- subject user and display name
- origin and destination Region
- federation session ID
- home Grid URL
- remote Service Grant ID
- short-lived service credential
- service scopes
- nonce
- issued/expiry timestamps

Lifetime is bounded to 30–900 seconds.

The verifier still accepts valid earlier OGL-FED/1 travel tokens for compatibility, but those tokens do not gain the v2 remote-service contract.

## Federation Service Grants

Every new outbound native travel request creates a short-lived home-grid grant.

Default scopes:

- `profile`
- `appearance`
- `inventory`
- `assets`
- `social`
- `presence`

Persistent home-grid grant state contains the grant ID, audience, subject, service-credential hash, scopes, expiry and revocation state.

Authorization requires a matching:

- grant ID
- audience Grid
- subject user
- requested scope
- non-expired/non-revoked grant
- presented service credential

The peer must also remain trusted.

A local user can revoke their own grant. Operators can inspect and revoke grants administratively.

## Foreign Sessions

The destination persists enough OGL-FED/2 context for the remote visitor to continue using granted home services after a destination restart.

Foreign Session state includes:

- issuer Grid
- remote subject
- display name
- origin/destination Region
- remote session ID
- home URL
- Service Grant ID
- service scopes
- bounded service credential
- lifecycle timestamps/state

Administrative session JSON exposes routing/scopes but does not expose the service credential.

## Remote service API

All peer service calls carry the grant ID, service credential, audience Grid and subject user.

| Endpoint | Scope | Purpose |
| --- | --- | --- |
| `POST /v1/federation/service/profile` | `profile` | identity + Appearance summary |
| `POST /v1/federation/service/appearance` | `appearance` | Appearance, wearables, attachments |
| `POST /v1/federation/service/inventory` | `inventory` | native Inventory tree |
| `POST /v1/federation/service/asset` | `assets` | exportable owned Asset data |
| `POST /v1/federation/service/social` | `social` | friend-relation metadata |
| `POST /v1/federation/service/presence` | `presence` | home-grid Presence/offline state |

### Asset rule

Remote Inventory visibility does not imply Asset export permission.

Remote Asset delivery requires:

1. the Asset belongs to the grant subject
2. the grant includes `assets`
3. the Asset has native `perm_export`

14.0 is a read-federation contract. It does not implement remote Inventory or Asset mutation.

## Travel acceptance

Inbound travel performs:

1. trusted-peer lookup
2. signature and audience verification
3. lifetime validation
4. nonce consumption / replay rejection
5. Foreign Session creation
6. destination Region/World resolution
7. Estate and Parcel entry checks
8. destination Scene Ticket issuance

The visitor then uses the native Scene Protocol v2 path from 13.0.

## Observability

14.0 exposes:

- federation peer count
- federation Service Grant count
- native Foreign Session count
- safe operator grant listing
- peer/session administration
- audit events for trust/revocation and grant revocation

## Hypergrid compatibility status

Hypergrid stays an explicit legacy compatibility bridge.

Existing compatibility includes Region discovery/linking, home travel, verification callbacks, Friends, IM, export-safe Assets, XInventory, AvatarService and persistent foreign service routes.

14.0 adds foreign-session identity collision protection: an existing foreign session ID cannot be replaced by a different Agent/HomeURI/service-token identity. Such an attempt fails with:

`foreign-session-collision`

An idempotent refresh of the same identity remains allowed.

### Important legacy boundary

Verified `/foreignagent` requests still return a non-success result after identity verification because the OpenSimulator legacy simulator/viewer data plane is not implemented.

Scene Protocol v2 is native OpenGenesisLINK and does not automatically make Firestorm/OpenSimulator viewers compatible.

## Canonical documents

- `docs/OGL-FED-v2.md`
- `docs/HYPERGRID-COMPAT-v0.md`
- `docs/SCENE-PROTOCOL-v2.md`
- `docs/VIEWER-CONTRACT-13.0.md`

`docs/OGL-FED-v0.md` is retained only as the historical OGL-FED/1 foundation document.

## Acceptance criteria

The final PR head must pass:

- Linux x86_64 warnings-as-errors build
- Linux ARM64/aarch64 warnings-as-errors build
- Windows x86_64/MSVC warnings-as-errors build
- unit tests
- World/Social/Handoff smoke
- Script World query/ACK smoke
- OGL/LSL ScriptEngine smoke
- Crossing v3 smoke
- Object Crossing smoke
- Viewer Bootstrap/Scene v2 smoke
- OGL-FED/2 Remote Services smoke
- SQLite integration
- PostgreSQL integration
- MariaDB integration

The dedicated Federation smoke verifies trust, v2 travel context, all remote read services, wrong-audience rejection, grant revocation and peer-revocation invalidation.

## Acceptance result

All required gates passed on final PR head `f1e4f5d2d47d9a0901d8677249e3b9be3367eee1`:

- Linux x86_64: PASS
- Linux ARM64/aarch64: PASS
- Windows x86_64/MSVC: PASS
- unit tests: PASS
- World/Social/Handoff smoke: PASS
- Script World query/ACK smoke: PASS
- OGL/LSL ScriptEngine smoke: PASS
- Crossing v3 smoke: PASS
- Object Crossing smoke: PASS
- Viewer Bootstrap/Scene v2 smoke: PASS
- OGL-FED/2 Remote Services smoke: PASS on x86_64 and ARM64
- SQLite integration: PASS
- PostgreSQL integration: PASS
- MariaDB integration: PASS

Canonical 14.0 squash merge:

`de66c4290fe9ab7dcc3fe06068b96dc6fe1f71ad`


## Explicit non-claims

14.0 does not claim:

- global federation PKI
- automatic DNS/WebPKI Grid trust enrollment
- long-lived unrestricted delegation
- cross-grid Inventory mutation
- cross-grid Asset mutation
- remote friend mutation
- economy/currency settlement
- stable 1.0 federation wire format
- complete OpenSimulator legacy simulator/viewer data plane
- OpenSimulator 0.9.3.x interoperability certification

## Next roadmap block

The next major server milestone is **15.0.0-dev — Platform Services Completion**.

Primary scope:

- production-complete Social/Groups/Land service contracts
- native transaction-ledger Economy foundation
- marketplace/service contracts
- permission/audit integration
- tests and persistence required before the later scaling, HA and performance phase
