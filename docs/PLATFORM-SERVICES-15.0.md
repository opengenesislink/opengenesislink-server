# OpenGenesisLINK Platform Services 15.0

## Scope

OpenGenesisLINK Server 15.0.0-dev introduces the first coherent native platform-services layer for local-grid economy, marketplace transactions, Social policy, Group invitations and Parcel user access control.

The milestone remains intentionally provider-neutral. It does not hard-code NV$, Stripe, PayPal or any other external payment processor/currency brand.

## Economy ledger

The native Economy layer uses integer minor units and a configurable uppercase currency code.

Core records:

- account balance
- mint entries
- burn entries
- direct transfer entries
- escrow reserve entries
- escrow commit entries
- escrow release entries

All mutating ledger operations use idempotency references. References are globally unique across normal entries and escrow reservations.

### Supply semantics

Mint increases total supply.

Burn decreases total supply.

Direct transfers preserve total supply.

Escrow reservation moves value out of the buyer's spendable balance into a reserved escrow state. Reserved value remains part of total supply.

Escrow commit moves reserved value into the seller balance.

Escrow release refunds reserved value to the buyer.

## Economy API

Authenticated user endpoints:

- `GET /v1/economy/wallet`
- `GET /v1/economy/ledger`
- `GET /v1/economy/escrows`
- `POST /v1/economy/transfer`

Public metadata:

- `GET /v1/economy/info`

Administrator-authorized operations:

- `POST /v1/economy/admin/mint`
- `POST /v1/economy/admin/burn`

Amounts are represented as integer minor units. Floating-point currency mutation is deliberately avoided.

## Marketplace

A Marketplace listing contains:

- seller
- Asset ID
- title
- description
- integer price
- currency code
- buyer after purchase
- sale reference
- lifecycle timestamps

Listing states:

```text
active -> reserved -> sold
   |
   +-------------> cancelled
```

A reserved listing cannot be purchased a second time.

### Purchase flow

```text
1. validate listing and seller Asset
2. reserve Marketplace listing
3. reserve buyer funds in Economy escrow
4. create buyer Asset copy through native Asset permissions
5. create buyer Inventory item
6. mark listing sold
7. commit escrow to seller
8. audit + seller notification
```

The source Asset must still belong to the seller and must have both copy and transfer permission.

### Compensation path

If a logical failure occurs before settlement completes, 15.0 attempts compensating rollback:

- remove generated buyer Inventory item
- remove the generated buyer-owned Asset copy
- release reserved escrow funds back to buyer
- reactivate the listing where applicable

This is application-level compensation. It is not a claim of a globally distributed ACID transaction across every file-backed store under abrupt process or host failure.

## Social policy

Persistent per-user Social policy supports:

- block
- mute

Block is directional as stored, but protected interactions test both directions.

A block prevents:

- friend request creation
- friend request acceptance
- direct messages
- Group invitations between the users
- direct wallet transfers between the users
- Marketplace purchase between the users

Enabling a block also removes an existing native friend relation.

Mute does not discard messages. It suppresses direct-message notification delivery from the muted sender to the muting user while retaining the DirectMessage record.

Endpoints:

- `GET /v1/social/policies`
- `POST /v1/social/block`
- `POST /v1/social/mute`

## Group invitation lifecycle

15.0 adds persistent Group invitations instead of relying only on direct member insertion.

Invitation fields include:

- Group
- invited user
- inviting user
- requested role
- state
- creation time
- expiry time
- resolution time

States:

- pending
- accepted
- revoked
- expired

Acceptance is bound to the invited user.

At acceptance time the inviter is revalidated. If the inviter no longer has invitation power, the invite cannot silently grant membership.

Invitation lifetime is bounded between 60 seconds and 30 days.

Endpoints:

- `GET /v1/groups/invites`
- `POST /v1/groups/invites`
- `POST /v1/groups/invites/accept`
- `POST /v1/groups/invites/revoke`

The Core maintenance loop marks expired pending invitations as expired.

## Parcel user access

15.0 adds explicit user access entries to the existing owner/group/public Parcel policy.

An access entry is either:

- allow
- deny

Entry precedence for Parcel entry:

1. Parcel owner always enters
2. explicit user allow/deny
3. public entry
4. matching Parcel Group membership
5. deny

Therefore an explicit user deny overrides a public Parcel.

Owner access is implicit and cannot be replaced with an access-list entry.

Endpoints:

- `GET /v1/parcels/{parcel-id}/access`
- `POST /v1/parcels/access`
- `POST /v1/parcels/access/remove`

Only the Parcel owner can inspect or mutate the explicit access list through this API.

## Persistence

15.0 adds file-backed persistent stores for:

- Social policies
- Economy ledger/accounts/escrows
- Marketplace listings

It also upgrades the existing Group and Parcel persistence formats while retaining the previous Group/Parcel records where supported by their loaders.

Production SQL migration of these new platform-service stores is not claimed by 15.0. The SQL-authoritative service set from 10.0 remains unchanged unless explicitly documented otherwise.

## Metrics

The Core exposes additional metrics including:

- Economy account count
- Economy entry count
- Economy total supply in minor units
- active Marketplace listing count
- Social policy count

`/v1/content/stats` also exposes corresponding Platform Services counts.

## Portability

During 15.0 validation, the accumulated Admin HTTP dispatcher exceeded an MSVC block-nesting compiler limit.

The fix was architectural rather than compiler-specific suppression: new Platform Services routing was extracted into a dedicated routing helper. The platform remains required to compile with warnings-as-errors on Linux x86_64, Linux ARM64/aarch64 and Windows x86_64/MSVC.

## Security boundaries

The Script VM does not receive direct Economy or Marketplace mutation rights in 15.0.

Mint and burn require administrator authorization.

Marketplace listing creation validates Asset ownership and permissions.

Marketplace purchase validates current seller ownership again before reserving funds.

Social policy is enforced server-side on the protected native interaction paths.

All successful security/economy/marketplace mutations that are relevant to operator traceability emit Audit events through the existing AuditStore.

## Explicit non-claims

15.0 does not claim:

- external fiat payment integration
- Stripe/PayPal integration
- exchange rates
- taxation/VAT accounting
- recurring subscriptions
- auctions/bidding
- rental contracts
- cross-grid currency settlement
- fraud scoring
- chargebacks
- globally distributed ACID transactions across independent stores
- crash-consistent two-phase commit between every store
- SQL-authoritative persistence for every new 15.0 Platform Service
- stable 1.0 economy/marketplace wire protocol
