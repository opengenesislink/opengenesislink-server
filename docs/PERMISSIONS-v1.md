# Permissions v1

OpenGenesisLINK `1.5.0-dev` adds the first common permission-mask foundation for Assets and Scene objects.

## Permission bits

```text
COPY      0x01
MODIFY    0x02
TRANSFER  0x04
EXPORT    0x08
```

Permission masks are currently development contracts and may be extended before a stable protocol release.

## Assets

Each Asset has an owner permission mask and a next-owner permission mask. Transfer requires `TRANSFER`. Keeping a source copy while transferring additionally requires `COPY`.

When ownership changes, the recipient receives the reduced next-owner mask. Blob content remains content-addressed, so a permitted copy does not duplicate identical binary data.

## Scene objects

Persistent Scene objects carry:

- owner user id
- optional Group id
- owner permissions
- Group permissions
- everyone permissions

Modify/delete authorization is evaluated server-side from authenticated identity and signed Group membership claims. Owners can update Group/everyone permission masks; assigning a Group requires authenticated membership in that Group.

The current Scene layer primarily enforces modify authority. Full object copy/transfer workflows, inventory-to-world rez semantics and nested/linkset permission propagation are not complete yet.
