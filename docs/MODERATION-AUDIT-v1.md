# Moderation and Audit v1

OpenGenesisLINK `1.5.0-dev` adds a first server-side moderation and audit layer.

## Bans

A ban targets a user and has either:

- `global` scope, or
- `region` scope with a Region id.

Bans may be permanent or have an expiry timestamp. Core rejects viewer-session, teleport and handoff requests that violate a ban. World also checks moderation during Scene join so bypassing Core does not grant access.

## Admin API

Development moderation/audit endpoints require the static `X-OpenGenesis-Admin-Key` header:

```text
GET  /v1/admin/moderation
POST /v1/admin/moderation/ban
POST /v1/admin/moderation/unban
GET  /v1/admin/audit
```

The repository configuration contains an explicit development placeholder key. Replace it before network exposure.

## Audit

Sensitive governance actions append audit events containing sequence, actor, action, target, detail and timestamp. The log is append-oriented and persistent.

This is not yet a complete security operations system. TLS, admin identities/roles, tamper-evident signing, log shipping, mute/block systems, abuse reports and rate limiting are still future server work.
