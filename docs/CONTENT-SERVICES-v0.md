# Content Services v0

OpenGenesisLINK `0.5.0-dev` contains the first native Asset and Inventory services.

## Asset Store

Asset metadata is persisted separately from binary content. Binary content is stored by SHA-256 content hash, enabling content-addressed deduplication while each Asset still receives its own Asset id and owner.

Metadata includes:

- Asset id
- owner user id
- name
- MIME type
- content SHA-256
- byte size
- creation time

Default development maximum Asset size is 1 MiB and is configurable with `assets.max_bytes`.

API:

```text
GET  /v1/assets
POST /v1/assets
GET  /v1/assets/<asset-id>
GET  /v1/content/stats
```

Asset endpoints require a bearer session except aggregate content statistics. The current API permits reading only Assets owned by the authenticated user.

## Inventory Store

Every identity receives a persistent root folder named `My Inventory`. The first implementation supports nested folders and items referencing Asset ids.

API:

```text
GET  /v1/inventory
POST /v1/inventory/folders
POST /v1/inventory/items
```

An Inventory item can only be created from an Asset owned by the authenticated user.

## Storage

Default Core paths:

```toml
[storage]
assets_metadata = "data/assets.db"
assets_blobs = "data/assets"
inventory = "data/inventory.db"
```

The metadata formats are early development formats and are not frozen compatibility contracts yet.
