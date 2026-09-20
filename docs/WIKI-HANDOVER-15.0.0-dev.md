# Wiki Handover — OpenGenesisLINK Server 15.0.0-dev

## Milestone

OpenGenesisLINK Server 15.0.0-dev is the **Platform Services Completion** milestone.

- repository: `opengenesislink/opengenesislink-server`
- pull request: #20
- version: `15.0.0-dev`
- implementation: C++23
- license: MPL 2.0
- native federation baseline: OGL-FED/2
- native Viewer/Scene server contract baseline: Viewer Bootstrap v1 + Scene Protocol v2

15.0 builds on the production foundation, World Runtime, ScriptEngine, Viewer/Scene contract and Federation stack by adding the first coherent native local-grid Economy, Marketplace and policy services.

## Major additions

### Native Economy

- provider-neutral currency code
- integer minor-unit balances
- persistent wallet accounts
- mint/burn
- direct transfers
- globally unique idempotency references
- persisted escrow reserve/commit/release
- total-supply accounting

### Native Marketplace

- active/reserved/sold/cancelled listing lifecycle
- seller Asset ownership/permission validation
- reservation before payment to reject double purchase
- Economy escrow before fulfillment
- buyer Asset copy
- buyer Inventory delivery
- seller settlement only after fulfillment and listing finalization
- compensating rollback for logical failure paths

### Social policy

- persistent block
- persistent mute
- bidirectional block enforcement on protected interactions
- existing friend relation removed when a block is enabled
- muted Direct Messages remain persisted while notifications are suppressed

### Group lifecycle

- persistent invitations
- member/officer invite roles
- bounded expiry
- accept
- revoke
- expiry maintenance
- target-user binding
- inviter authorization revalidation at acceptance

### Parcel access

- explicit user allow
- explicit user deny
- owner-only ACL management
- explicit deny precedence over public entry
- implicit owner access

## Platform Services API

```text
GET  /v1/social/policies
POST /v1/social/block
POST /v1/social/mute

GET/POST /v1/groups/invites
POST     /v1/groups/invites/accept
POST     /v1/groups/invites/revoke

GET  /v1/parcels/{parcel-id}/access
POST /v1/parcels/access
POST /v1/parcels/access/remove

GET  /v1/economy/info
GET  /v1/economy/wallet
GET  /v1/economy/ledger
GET  /v1/economy/escrows
POST /v1/economy/transfer
POST /v1/economy/admin/mint
POST /v1/economy/admin/burn

GET/POST /v1/marketplace/listings
GET      /v1/marketplace/mine
POST     /v1/marketplace/cancel
POST     /v1/marketplace/purchase
```

## Economy transaction model

```text
Buyer wallet
    |
    | reserve
    v
Escrow --------------+
    |                 |
    | commit          | release
    v                 v
Seller wallet     Buyer refund
```

Marketplace purchase:

```text
active listing
   |
   v
reserved listing
   |
   +--> escrow reserve
   |
   +--> Asset copy to buyer
   |
   +--> Inventory item to buyer
   |
   +--> listing sold
   |
   +--> escrow commit to seller
```

Logical failures before successful settlement use compensation where possible.

This is not a globally distributed ACID transaction guarantee across abrupt process/host failure.

## Persistence and restart behavior

Unit coverage verifies restart persistence for:

- Social block/mute
- Group invitations
- Parcel access entries
- Economy balances
- Economy escrow state
- Marketplace final state

The 15.0 Platform Services themselves are file-backed in this milestone unless an existing store explicitly uses the SQL storage foundation. 15.0 does not claim new SQL-authoritative migrations for all Platform Services.

## Compatibility

No new Economy or Marketplace behavior is injected into Hypergrid.

OGL-FED/2 remains independent and 15.0 does not claim cross-grid currency settlement.

The native Viewer can consume the new Core APIs, but 15.0 is a server milestone and does not claim the separate Viewer application implements a Marketplace UI.

## Canonical documents

- `docs/PLATFORM-SERVICES-15.0.md`
- `docs/OGL-FED-v2.md`
- `docs/SCENE-PROTOCOL-v2.md`
- `docs/VIEWER-CONTRACT-13.0.md`
- `docs/SCRIPT-COMMAND-STATUS-12.0.md`

## Acceptance criteria

The final PR head must pass:

- Linux x86_64 warnings-as-errors build
- Linux ARM64/aarch64 warnings-as-errors build
- Windows x86_64/MSVC warnings-as-errors build
- all unit tests
- World/Social/Handoff smoke
- Script World query/ACK smoke
- OGL/LSL ScriptEngine smoke
- Crossing v3 smoke
- Object Crossing smoke
- Viewer Bootstrap/Scene v2 smoke
- OGL-FED/2 Remote Services smoke
- dedicated 15.0 Platform Services smoke
- SQLite integration
- PostgreSQL integration
- MariaDB integration

The dedicated 15.0 smoke verifies:

- Social block enforcement
- Social mute notification suppression
- Group invitation acceptance
- Wallet mint
- wallet transfer
- block enforcement on wallet transfer
- Marketplace listing
- escrow-backed purchase
- buyer Asset delivery
- buyer Inventory delivery
- seller settlement
- duplicate purchase rejection
- insufficient-funds purchase rollback to active listing
- committed escrow visibility
- Platform Services stats

## Acceptance result

The exact final tested PR head and CI result are intentionally recorded by the post-merge documentation-only follow-up after all required CI gates pass. This prevents documentation from claiming acceptance before the final code/document head is actually tested.

## Explicit non-claims

15.0 does not claim:

- fiat payment processor integration
- cross-grid economy settlement
- globally distributed ACID semantics across all independent stores
- crash-consistent two-phase commit across every persistence store
- taxation or invoicing
- auctions
- recurring billing
- final Viewer Marketplace UI
- stable 1.0 Economy/Marketplace API
- complete SQL-authoritative migration of every new 15.0 store
