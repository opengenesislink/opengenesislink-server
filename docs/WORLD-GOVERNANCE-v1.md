# World Governance v1

OpenGenesisLINK `1.5.0-dev` introduces the first persistent world-governance layer shared by Core and World Runtime.

## Groups

Groups are persistent Core objects. Every Group has a founder and members with a role and power mask.

Initial roles:

- `owner` — all current Group powers
- `officer` — operational powers used for member/role management
- `member` — normal membership

Current powers cover member invitation/ejection, role changes, land, object and Group-chat authority. The API currently performs direct member management; invitation acceptance workflows and richer role definitions are later layers.

A Scene Ticket contains the authenticated user's Group ids. The World Node uses those signed claims for Group-based Parcel and object checks rather than accepting arbitrary Group ids from the client.

## Parcels

The first Parcel implementation uses non-overlapping axis-aligned rectangles inside a Region. A Parcel stores:

- Region and Parcel id
- owner user id
- optional Group id
- rectangle coordinates
- public entry policy
- public build policy
- Group build policy
- Group terraform policy

Parcel policy is checked by Core before issuing viewer/teleport/handoff tickets and by World at Scene join and relevant build/terraform operations.

The current model is a land-policy foundation. Polygon parcels, subdivision/join, sale/rental, traffic accounting, ban/access lists and Estate policy are not implemented yet.

## Storage boundary

Core and World currently read the same Parcel and moderation files on a single host. World reloads policy before sensitive checks. This is suitable for the current development topology, not a distributed policy-replication design. Multi-host consistency and federation policy distribution are later work.
