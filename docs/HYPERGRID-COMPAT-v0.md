# Hypergrid Compatibility Foundation v0

OpenGenesisLINK keeps OpenSimulator Hypergrid compatibility separate from native `OGL-FED/1`.

The compatibility layer follows the OpenSimulator Gatekeeper/UserAgent control-plane conventions without importing OpenSimulator code.

Implemented through 3.5.0-dev:

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
- cross-platform tests

The OpenSimulator reference behavior uses XML-RPC for Gatekeeper/UserAgent calls and JSON for `/foreignagent` agent creation. OpenGenesisLINK now parses and verifies that foreign-agent identity flow, but still returns a non-success result after verification because the legacy simulator/viewer data plane is not complete. This prevents a false compatibility claim.

Still open:

- legacy simulator/viewer data-plane handoff after verified `/foreignagent`
- complete return-home routing beyond home-session verification/logout
- HG Friends
- HG Instant Messaging
- remote Assets/Inventory
- legacy simulator/viewer data-plane compatibility

OGL-FED Ed25519 keys are never reused as Hypergrid secrets or session tokens.
