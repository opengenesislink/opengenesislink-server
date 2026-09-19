# Hypergrid XInventory v2

OpenGenesisLINK 5.5.0-dev extends the legacy OpenSimulator XInventory adapter with guarded write support.

## Default

Writes remain disabled by default:

```toml
[hypergrid]
inventory_write_enabled = false
```

Read operations continue to work when Hypergrid compatibility is enabled.

## Supported reads

- CREATEUSERINVENTORY
- GETROOTFOLDER
- GETINVENTORYSKELETON
- GETFOLDERCONTENT
- GETFOLDERITEMS
- GETFOLDER
- GETITEM
- GETASSETPERMISSIONS

## Supported writes when enabled

- ADDFOLDER
- UPDATEFOLDER
- MOVEFOLDER
- DELETEFOLDERS
- PURGEFOLDER
- ADDITEM
- UPDATEITEM
- MOVEITEMS
- DELETEITEMS

## Legacy UUID aliases

OpenSimulator supplies stable UUIDs for Inventory folders and items. Native OpenGenesisLINK Inventory IDs use their own representation.

Inventory persistence v2 therefore stores an optional `legacy_id` for folders and items.

Rules:

- native-created entries without a legacy alias keep deterministic compatibility UUID mapping
- legacy-created entries retain the exact remote UUID
- aliases survive Core restart
- aliases remain scoped to their local owner
- older Inventory v1 records remain readable

## Write validation

Write operations validate:

- local owner identity
- destination parent ownership
- no folder self-parenting
- no folder cycles
- referenced Item ownership
- referenced local Asset ownership
- Asset Export permission
- Asset Transfer permission for write/import binding

The adapter does not permit arbitrary remote Asset IDs to become local Inventory items.

## Security warning

OpenSimulator's legacy XInventory form protocol does not provide the same authorization envelope as native OpenGenesisLINK APIs.

For that reason:

- write mode is explicit opt-in
- default is read-only
- use the compatibility listener only on a trusted network or behind a restrictive reverse proxy/firewall when writes are enabled
- stronger service-to-service authentication remains required before treating this surface as production-ready on an untrusted network

## Persistence

Inventory store format v2 adds persisted legacy aliases while retaining v1 loading compatibility.
