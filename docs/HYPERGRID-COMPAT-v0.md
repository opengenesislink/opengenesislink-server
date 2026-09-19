# Hypergrid Compatibility Foundation v0

OpenGenesisLINK keeps OpenSimulator Hypergrid compatibility separate from native `OGL-FED/1`.

The compatibility layer follows the OpenSimulator Gatekeeper/UserAgent control-plane conventions without importing OpenSimulator code.

Implemented through 6.0.0-dev:

- XML-RPC method parsing and struct responses
- `link_region`
- `get_region`
- `get_server_urls`
- deterministic legacy UUID mapping for native OpenGenesisLINK Region IDs
- legacy Region handle mapping from native grid coordinates
- explicit enable/disable configuration
- persistent home-grid travel sessions and OpenSim-style service tokens
- `verify_agent`, `verify_client`, `agent_is_coming_home` and `logout_agent`
- `/foreignagent` JSON circuit ingestion
- HomeURI verification callback before a foreign identity is recorded
- service-token destination binding
- persistent verified foreign visitor records with expiry cleanup
- Core API for Hypergrid travel/session state
- Hypergrid Friends `/hgfriends` adapter backed by the native FriendsStore
- HG friend permission lookup, new/delete friendship, friendship offer validation and status notifications
- incoming HG friendship offers mapped to native notifications
- export-safe `/assets/<uuid>`, `/data` and `/metadata` GET compatibility
- deterministic legacy Asset UUID mapping
- native Export permission enforcement before remote Asset delivery
- incoming `grid_instant_message` mapped into native messages and notifications
- outbound HG IM routing using stored `IMServerURI`
- `/xinventory` read support for root, skeleton, folders, items and Asset permissions
- opt-in XInventory writes for folder/item add, update, move, purge and delete
- stable persisted legacy folder/item UUID aliases across Core restarts
- server-side owner, parent/cycle and Asset export/transfer permission validation for XInventory writes
- shared-service-key authentication for writable XInventory with constant-time key comparison
- `/avatar` AvatarService exchange for AvatarHeight, VisualParams, wearables and attachments
- persistent foreign Asset/Inventory/Avatar/IM service routing URLs
- `get_home_region` and persistent return-home travel state
- authenticated Core return-home and outbound HG IM controls
- shared portable HTTP/HTTPS callback client with certificate-chain and hostname verification
- cross-platform tests

The OpenSimulator reference behavior uses XML-RPC for Gatekeeper/UserAgent calls and JSON for `/foreignagent` agent creation. OpenGenesisLINK now parses and verifies that foreign-agent identity flow, but still returns a non-success result after verification because the legacy simulator/viewer data plane is not complete. This prevents a false compatibility claim.

Still open:

- final legacy simulator/viewer data-plane handoff after verified `/foreignagent`
- per-grid signed XInventory requests, nonces/replay protection and remote-grid allowlists beyond the current shared-key foundation
- broader legacy Appearance edge cases and baking compatibility
- real OpenSimulator 0.9.3.x end-to-end interoperability validation

OGL-FED Ed25519 keys are never reused as Hypergrid secrets or session tokens.


## XInventory write mode

Legacy Inventory mutation is disabled by default:

```toml
[hypergrid]
inventory_write_enabled = false
inventory_write_secret = ""
```

When write mode is enabled, `inventory_write_secret` must contain at least 24 bytes and each legacy write request must provide the matching `SERVICEKEY` form field. Keys are compared in constant time before mutation is attempted. Use HTTPS or a private authenticated transport so the shared key is not exposed, and continue restricting the HG listener at the network/reverse-proxy layer. Native OGL-FED credentials are not reused for this compatibility mechanism.
