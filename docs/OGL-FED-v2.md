# OpenGenesisLINK OGL-FED/2 — Native Federation Contract

OpenGenesisLINK 14.0.0-dev promotes the native federation contract from the OGL-FED/1 foundation to **OGL-FED/2**.

OGL-FED remains completely separate from the OpenSimulator Hypergrid compatibility bridge.

## Goals

OGL-FED/2 defines the server contracts required for a user to travel from one trusted OpenGenesisLINK Grid to another while the destination can continue to access explicitly granted home-grid services.

The contract covers:

- Grid identity
- peer trust and revocation
- signed travel authorization
- replay protection
- foreign sessions
- destination Scene entry
- scoped home-grid profile/Appearance access
- scoped home-grid Inventory access
- scoped exportable Asset access
- scoped Social metadata access
- scoped Presence access
- service-grant revocation and expiry

## Grid identity

Each Grid owns a persistent Ed25519 key pair.

Federation identity contains:

- Grid ID
- base URL
- Ed25519 public key
- Ed25519 private key stored only on the local Grid

Private keys are never transmitted as federation metadata.

The public federation information endpoint advertises:

```text
GET /v1/federation/info
```

with protocol identifier:

```text
OGL-FED/2
```

## Peer trust

A trusted peer record contains:

- Grid ID
- HTTPS base URL
- pinned Ed25519 public key
- trusted/revoked state
- update timestamp

### Key pinning

14.0 prevents silent key replacement.

If a currently trusted, non-revoked Grid is submitted again with a different public key, trust update fails with:

```text
federation-peer-key-change-requires-revocation
```

A legitimate key rotation therefore requires:

1. explicitly revoke the current peer
2. verify the new Grid key out-of-band
3. trust the peer again with the new key

This prevents accidental or malicious in-place key substitution through the administrative trust API.

## Peer revocation

Revoking a Grid now cascades into:

- the peer trust record
- active inbound foreign sessions issued by that Grid
- outstanding outbound service grants whose audience is that Grid

Already-issued Scene Tickets remain bounded by their short Scene-Ticket expiry and are not retroactively rewritten.

## OGL-FED/2 Travel Token

Travel Tokens remain Ed25519-signed, short-lived and audience-bound.

OGL-FED/2 token claims include:

- issuer Grid
- audience Grid
- subject user
- display name
- origin Region
- destination Region
- remote session ID
- home Grid base URL
- remote service grant ID
- remote service token
- remote service capability list
- nonce
- issued-at
- expiry

Tokens are valid for 30–900 seconds.

The destination verifies:

- signature
- trusted issuer
- issuer key
- audience
- lifetime
- nonce
- replay state

The token nonce is consumed once.

OGL-FED/2 verification remains able to parse earlier OGL-FED/1 travel tokens that do not contain remote-service context. This is a compatibility path, not a promise that OGL-FED/1 exposes the new services.

## Remote Service Grant

When Core issues native federation travel, it also creates a persistent home-grid service grant.

Default scopes:

```text
profile
appearance
inventory
assets
social
presence
```

The clear service token is included in the signed Travel Token so the trusted destination can use it.

The home Grid stores only:

- grant ID
- audience Grid
- subject user
- SHA-256 hash of the service token
- scope list
- creation time
- expiry
- revoked state/time

The clear token is not stored in the home-grid grant database.

Authorization requires an exact match for:

- grant ID
- service token hash
- trusted/non-revoked audience Grid
- subject user
- requested scope
- non-expired/non-revoked grant

## Foreign session

After travel acceptance, the destination persists a foreign session containing:

- issuer Grid
- remote user
- display name
- origin/destination Regions
- remote session ID
- home Grid URL
- service grant ID
- service token
- granted service scopes
- session lifecycle

The service token is intentionally not returned by the destination's administrative session-list JSON.

The destination persistence file is secret-bearing because it must retain the clear service token across a destination restart. Operators must protect that store with the same host/filesystem controls used for other runtime secrets.

## Native remote service endpoints

The following endpoints are home-grid peer-to-peer contracts.

Each request must provide:

- `grant_id`
- `service_token`
- `audience_grid`
- `subject_user`

### Profile

```text
POST /v1/federation/service/profile
```

Requires scope:

`profile`

Returns native user identity and the current Appearance summary.

### Appearance

```text
POST /v1/federation/service/appearance
```

Requires scope:

`appearance`

Returns:

- Appearance revision
- avatar height
- visual params
- wearables
- attachment references

### Inventory

```text
POST /v1/federation/service/inventory
```

Requires scope:

`inventory`

Returns the subject user's native Inventory root/folders/items.

### Asset

```text
POST /v1/federation/service/asset
```

Requires scope:

`assets`

Additionally requires:

- requested Asset belongs to the subject user
- Asset has native `perm_export`

The response returns Asset metadata plus Base64 content.

A non-exportable Asset is not federated merely because it is referenced by Inventory.

### Social

```text
POST /v1/federation/service/social
```

Requires scope:

`social`

Returns the subject user's native friend-relation metadata without exposing interoperability secrets.

### Presence

```text
POST /v1/federation/service/presence
```

Requires scope:

`presence`

Returns the subject user's current home-grid Presence if online, otherwise `online=false`.

## Grant administration

Operators can inspect safe grant metadata through:

```text
GET /v1/federation/service-grants
```

The clear service token is never included.

A local user may revoke their own grant. Operators can revoke grants administratively:

```text
POST /v1/federation/service-grants/revoke
```

Peer revocation also revokes all outstanding grants for that audience.

Expired grants are removed by normal Core maintenance.

## Travel acceptance and Scene entry

The destination:

1. verifies trusted issuer and signed Travel Token
2. consumes the nonce
3. creates a persistent foreign session
4. verifies destination Region availability
5. applies Estate/Parcel entry policy
6. issues a short-lived destination Scene Ticket
7. returns Scene endpoint and service context to the destination-side caller

The remote user then enters the native Scene Protocol v2 path.

OGL-FED does not reuse Hypergrid AgentCircuitData or Hypergrid service tokens.

## Observability

Core exposes:

- federation peer count
- foreign session count
- federation service grant count

The service-grant list exposes only metadata/hashes-safe state and never the clear token.

## Security boundaries

OGL-FED/2 intentionally does not provide a universal bearer credential.

A service token is bound to one:

- subject
- audience Grid
- scope set
- expiry

A stolen token cannot be retargeted to another trusted Grid by simply changing the request's `audience_grid`.

Peer key replacement cannot occur silently.

Asset export permission remains authoritative even when Inventory contains the Asset.

## OGL-FED versus Hypergrid

OGL-FED/2 is the native OpenGenesisLINK federation protocol.

Hypergrid is an explicit compatibility adapter.

Do not reuse:

- OGL-FED Ed25519 private keys
- OGL-FED service tokens
- Scene Tickets

as Hypergrid shared secrets, service session IDs or XInventory keys.

## Explicit non-claims

14.0 does not claim:

- global PKI or automatic WebPKI-to-Grid trust enrollment
- automatic DNS-based federation trust
- long-lived delegation beyond bounded travel grants
- cross-grid Asset mutation
- cross-grid Inventory mutation
- cross-grid friend creation/removal through OGL-FED
- cross-grid economy/currency settlement
- end-to-end encrypted remote service payloads above HTTPS
- stable 1.0 federation wire format

Those can be layered on the OGL-FED/2 trust and service-grant foundation later.
