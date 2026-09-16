# Presence and Social Services v1

OpenGenesisLINK `1.5.0-dev` introduces the first integrated Presence, Friends and Direct Messaging services.

## Presence

World Nodes send periodic complete Presence snapshots per Region to the Core. Each online Presence contains:

- authenticated user id
- display name
- Region id
- World Node id and generation
- Scene entity id
- current x/y/z position
- last Core update time

A newer snapshot replaces the previous Presence set for that Region/session. When a World Node lease expires or disconnects, its Presence entries are removed.

API:

```text
GET /v1/presence
```

The endpoint requires an authenticated bearer session. Aggregate Presence count is included in `GET /v1/status`.

## Friends

Friend relationships are persisted by the Core. The first implementation supports pending requests and accepted relationships.

```text
GET  /v1/social/friends
POST /v1/social/friends/request
POST /v1/social/friends/accept
POST /v1/social/friends/remove
```

Friend requests and accepted relationships survive Core restarts.

## Direct messages

Direct messages are persisted with sender, recipient, text, send time and read time. In this development version, direct messages are only accepted between users with an accepted friendship.

```text
GET  /v1/social/messages
POST /v1/social/messages
POST /v1/social/messages/read
GET  /v1/social/stats
```

Message text is currently limited to 2000 bytes after control-character normalization.

## Current limitations

This is not yet a complete abuse/moderation system. Blocks, mutes, group chat, delivery receipts, push notifications, retention policies and federation are later layers.
