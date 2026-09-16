# World Operations v1

OpenGenesisLINK `2.0.0-dev` adds the first persistent server-side world-operations layer on top of governance, authenticated travel and content services.

## Estates and Region policy

An Estate has an owner and optional managers. Regions can be attached to an Estate and receive persistent operational policy:

- public/private access
- fly permission metadata
- script permission metadata
- voice permission metadata
- maximum agent capacity
- maturity level
- default landing coordinates

Core checks Estate public access and Region capacity before viewer session, teleport and Region handoff tickets are issued. Estate managers can update policy after the owner grants manager status.

## Landmarks

Users can create persistent Landmarks containing a name, Region id and local coordinates. `POST /v1/viewer/teleport` accepts either explicit Region coordinates or a user-owned `landmark_id`.

## Notifications

The persistent notification store currently receives events for:

- friend requests
- accepted friend requests
- direct messages
- Group membership
- Group notices

Notifications have unread/read state and survive Core restarts.

## Group channel and notices

Groups have a persistent channel. Members with chat power can post chat records; officers/owners receive notice power and can publish Group notices. Notices generate persistent notifications for other Group members.

## Metrics

`GET /metrics` exposes a Prometheus-compatible text endpoint with Core uptime and aggregate World, Region, Presence, Session, Governance, Estate, Content and Social counters. Per-Region simulator FPS, entity and avatar gauges are included.

The metrics endpoint is intentionally low-cardinality and does not expose user ids, usernames, session tokens or message contents.

## Current boundaries

The Estate flags for fly/scripts/voice are persistent policy metadata in this milestone. Public access and maximum Region capacity are actively enforced by Core ticket issuance. Fine-grained runtime enforcement for script execution, voice sessions and flight movement belongs to the corresponding runtime subsystems when those layers are implemented.
