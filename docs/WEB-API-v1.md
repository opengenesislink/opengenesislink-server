# OpenGenesisLINK Core Web API v1

`1.0.0-alpha.1` exposes the versioned Core browser dashboard and JSON API. The default bind address remains `127.0.0.1:18080`. Internet-facing deployments should place it behind production TLS/reverse-proxy policy.

## Browser dashboard

`GET /` displays Core version/uptime, World Nodes, Regions, simulation metrics, online Presence, identity/session, Social, content, Group/Parcel and moderation/audit counters.

## Discovery and status

```text
GET /health
GET /v1
GET /v1/release
GET /v1/status
GET /v1/atlas/bootstrap
GET /v1/atlas/regions
GET /v1/atlas/regions/<region-id>
GET /v1/worlds
GET /v1/regions
GET /v1/regions/<region-id>/neighbors
GET /metrics
```

## Identity and world travel

```text
POST /v1/auth/register
POST /v1/auth/login
GET  /v1/auth/me
POST /v1/auth/logout
POST /v1/viewer/session
POST /v1/viewer/teleport
POST /v1/viewer/handoff
GET  /v1/presence
```

Core checks account session, moderation state, Region availability and Parcel-entry policy before issuing a Scene Ticket. Tickets carry server-selected spawn coordinates and authenticated Group memberships.

`viewer/handoff` additionally requires a cardinally adjacent online Region. The current handoff is client-driven rather than a seamless atomic simulator crossing.

## Groups and land

```text
GET/POST /v1/groups
GET      /v1/groups/<group-id>/members
POST     /v1/groups/members
POST     /v1/groups/role
POST     /v1/groups/remove
GET      /v1/groups/<group-id>/channel
POST     /v1/groups/channel
POST     /v1/parcels
GET      /v1/regions/<region-id>/parcels
GET      /v1/parcels/owned
POST     /v1/parcels/policy
GET/POST /v1/estates
POST     /v1/estates/managers
POST     /v1/estates/regions
POST     /v1/estates/regions/policy
GET      /v1/regions/<region-id>/estate
GET/POST /v1/landmarks
POST     /v1/landmarks/remove
```

## Social

```text
GET  /v1/social/friends
POST /v1/social/friends/request
POST /v1/social/friends/accept
POST /v1/social/friends/remove
GET  /v1/social/messages
POST /v1/social/messages
POST /v1/social/messages/read
GET  /v1/social/stats
GET  /v1/notifications
POST /v1/notifications/read
```

## Content

```text
GET  /v1/content/stats
GET  /v1/assets
POST /v1/assets
GET  /v1/assets/<asset-id>
POST /v1/assets/transfer
GET  /v1/inventory
POST /v1/inventory/folders
POST /v1/inventory/items
```

Asset JSON includes current and next-owner permission masks.

## Moderation and audit

The development admin routes require `X-OpenGenesis-Admin-Key`.

```text
GET  /v1/admin/moderation
POST /v1/admin/moderation/ban
POST /v1/admin/moderation/unban
GET  /v1/admin/audit
```

## Security status

The interface has authenticated bearer sessions, signed Scene Tickets, server-side capabilities, Parcel/object policy enforcement, Estate admission/capacity checks and an admin-key boundary. It remains a development API. Keep it on trusted interfaces. TLS termination, key rotation, distributed replay state, mature admin roles and rate limiting are not complete.


## First-release client contracts

`GET /v1/release` is the machine-readable compatibility contract for separate Viewer and Atlas projects.

The first dedicated Atlas contract is `ogl-atlas-v1`:

```text
GET /v1/atlas/bootstrap
GET /v1/atlas/regions
GET /v1/atlas/regions/<region-id>
```

These endpoints are public read contracts and expose Region/Parcel discovery data, not exact individual Presence locations.
