# Region Handoff v0

OpenGenesisLINK `1.5.0-dev` contains authenticated teleport and the current adjacent-Region handoff foundation.

## Movement boundary

The Scene protocol accepts `AVATAR_MOVE`. Region Runtime updates the authenticated Avatar entity and reports `north`, `south`, `east` or `west` when movement crosses a Region edge. The current avatar position is clamped while handoff is prepared.

## Neighbor discovery

Core derives cardinally adjacent Regions from grid coordinates:

```text
GET /v1/regions/<region-id>/neighbors
```

## Teleport

An authenticated user can request a server-selected destination spawn through:

```text
POST /v1/viewer/teleport
```

Before issuing a ticket, Core checks target Region availability, moderation state and Parcel entry policy.

## Adjacent handoff

```text
POST /v1/viewer/handoff
```

Core validates adjacency and target policy, selects an edge-appropriate target spawn and issues a short-lived ticket bound to the destination Region. The ticket also identifies the source Region.

The target World validates the same ticket security and governance policy before creating Presence.

## Current limitations

This is still a client-driven secure handoff, not seamless simulator crossing. Atomic source/destination state transfer, attachment transfer, velocity continuation, rollback, distributed transaction state and federation handoff are not complete.
