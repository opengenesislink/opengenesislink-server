# Core Admin / Status API

The Core development HTTP surface is available on loopback by default at `http://127.0.0.1:18080`.

`1.5.0-dev` exposes a browser dashboard plus JSON status for:

- Core uptime and version
- World Node state/generation
- Region state and simulation metrics
- live authenticated Presence count
- identities and sessions
- friendship/message aggregates
- Asset and Inventory counts
- Group and Parcel counts
- active moderation bans and audit-event count

Primary endpoints:

```text
GET /health
GET /v1
GET /v1/status
GET /v1/worlds
GET /v1/regions
GET /v1/content/stats
```

Governance and moderation endpoints are documented in `WEB-API-v1.md`, `WORLD-GOVERNANCE-v1.md` and `MODERATION-AUDIT-v1.md`.

This remains a development interface. It should not be bound directly to an untrusted public network without TLS termination, replacement of development secrets, mature admin authorization and rate limiting.
