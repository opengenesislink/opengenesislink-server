# Hypergrid Compatibility Foundation v0

OpenGenesisLINK keeps OpenSimulator Hypergrid compatibility separate from native `OGL-FED/1`.

The compatibility layer follows the OpenSimulator Gatekeeper/UserAgent control-plane conventions without importing OpenSimulator code.

Implemented in 3.0.0-dev:

- XML-RPC method parsing and struct responses
- `link_region`
- `get_region`
- `get_server_urls`
- deterministic legacy UUID mapping for native OpenGenesisLINK Region IDs
- legacy Region handle mapping from native grid coordinates
- explicit enable/disable configuration
- cross-platform tests

The OpenSimulator reference behavior uses XML-RPC for Gatekeeper/UserAgent calls and JSON for `/foreignagent` agent creation. The latter is intentionally not marked complete yet.

Still open:

- `/foreignagent` JSON circuit ingestion
- callback verification through `verify_agent` / `verify_client`
- home-agent logout/return flow
- HG Friends
- HG Instant Messaging
- remote Assets/Inventory
- legacy simulator/viewer data-plane compatibility

OGL-FED Ed25519 keys are never reused as Hypergrid secrets or session tokens.
