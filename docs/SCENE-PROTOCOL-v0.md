# Scene Protocol v0

The Scene endpoint is an early native OpenGenesisLINK protocol over the OGL binary frame transport.

## Authentication and authorization

A client first exchanges `HELLO`, then sends `SCENE_JOIN` with a Core-issued Scene Ticket. World verifies signature, expiry, Region binding, nonce replay and `scene.join`. It also checks moderation and Parcel entry policy.

The ticket carries authenticated user id/display name, Group memberships, server-selected spawn coordinates and an explicit capability set. Clients cannot grant themselves Group membership or choose an unchecked spawn position.

Current capabilities include:

```text
scene.join
scene.read
scene.move
scene.chat
scene.object.create
scene.object.modify.own
scene.object.permissions
scene.terrain.sample
scene.terrain.modify
```

Capability checks are combined with Parcel and object permission policy.

## Messages

| Message | Value | Direction |
| --- | ---: | --- |
| SCENE_JOIN | 100 | client → World |
| SCENE_JOIN_ACK | 101 | World → client |
| SCENE_SNAPSHOT_REQUEST | 102 | client → World |
| SCENE_SNAPSHOT | 103 | World → client |
| ENTITY_CREATE | 110 | client → World |
| ENTITY_CREATE_ACK | 111 | World → client |
| ENTITY_UPDATE | 112 | client → World |
| ENTITY_UPDATE_ACK | 113 | World → client |
| ENTITY_DELETE | 114 | client → World |
| ENTITY_DELETE_ACK | 115 | World → client |
| ENTITY_PERMISSIONS | 116 | client → World |
| ENTITY_PERMISSIONS_ACK | 117 | World → client |
| CHAT_SEND | 120 | client → World |
| CHAT_EVENT | 121 | World → client |
| SCENE_EVENTS_REQUEST | 130 | client → World |
| SCENE_EVENTS | 131 | World → client |
| TERRAIN_SAMPLE_REQUEST | 140 | client → World |
| TERRAIN_SAMPLE | 141 | World → client |
| TERRAIN_SET_REQUEST | 142 | client → World |
| TERRAIN_SET_ACK | 143 | World → client |
| AVATAR_MOVE | 150 | client → World |
| AVATAR_MOVE_ACK | 151 | World → client |

Object create/update/delete and terrain mutation are subject to signed capability claims plus land/object policy. Owners can change Group/everyone object permission masks through `ENTITY_PERMISSIONS`.

`AVATAR_MOVE` updates position/velocity and returns a boundary direction when an edge is crossed.

The current text payload syntax remains a bootstrap format and is not a frozen long-term wire contract.
