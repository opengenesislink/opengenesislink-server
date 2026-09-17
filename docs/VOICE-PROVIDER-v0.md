# OpenGenesisLINK Voice Provider Contract v0

OpenGenesisLINK Server does not embed or require a proprietary voice backend. Voice is a destination capability supplied by a provider implementing the OpenGenesisLINK voice contract.

Protocol identifiers:

- `OGL-VOICE/1` — Grid/Core to Voice Provider contract
- `OGL-VOICE-CAP/1` — short-lived Viewer voice capability contract

## Design rules

1. Provider credentials stay on the Grid/Core side and are never sent to a Viewer.
2. A Viewer receives only a short-lived voice capability/token for its current destination.
3. Voice authorization follows the destination Region or Parcel, not the avatar's home Grid.
4. A foreign OGL-FED or legacy-Hypergrid guest may receive destination Voice after the Grid has authenticated the guest session.
5. Region and Parcel voice are separate scopes.
6. Provider implementations are replaceable. A Grid may use a hosted provider, a self-hosted provider, or disable Voice.
7. The OpenGenesisLINK Server remains provider-neutral. The planned hosted OpenGenesisLINK Voice service is a separate project.

## Federation flow

When an avatar moves from Grid A to Grid B, Grid B validates the travel/guest session first. Grid B then requests a new short-lived voice session for its own Region. The voice token from Grid A is never trusted as authorization for Grid B.

This permits seamless voice when both Grids use the same hosted provider and also permits a provider switch when they do not.

## Suggested configuration

```toml
[voice]
enabled = true
provider = "opengenesislink"
service_url = "https://voice.opengenesislink.de"
sip_domain = "sip.opengenesislink.de"
client_id = "grid_example"
client_secret = "replace-with-provider-secret"
token_lifetime_seconds = 300
```

The hosted service and its commercial/free quotas are intentionally outside the MPL-covered server implementation. The server-side provider interface remains open so operators can implement or self-host compatible providers.
