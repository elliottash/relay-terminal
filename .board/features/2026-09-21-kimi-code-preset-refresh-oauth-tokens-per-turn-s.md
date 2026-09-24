---
id: 9R2V
type: work
status: discussing
labels: [feature, providers]
rank: zzzzzzzzzzzzzzzzw
created: '2026-09-21'
source: pane c9b03025, 2026-09-21
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# kimi-code preset: refresh OAuth tokens per turn so panes don't 401 after ~15 min

## Issue
can you try my kimi key again, is k2.8 preview available → yeah try that (wire K2.8 Preview into Relay)
can you try my kimi key again, is k2.8 preview available → yeah try that (wire K2.8 Preview into Relay)

## Discussion points
Findings from wiring it up by hand (2026-09-21):

- K2.8 Preview is `kimi-for-coding` on the Kimi Code subscription endpoint (`https://api.kimi.ai/coding/v1`), already in `MODEL_CATALOG["kimi-code"]`. It is NOT on the Moonshot platform (`api.moonshot.ai` lists only kimi-k3, kimi-k2.7-code[-highspeed], kimi-k2.6).
- The kimi-code endpoint and `auth.kimi.ai` are behind Cloudflare and 403 (error 1010) default Python library user agents; `User-Agent: Relay/0.1` and `kimi-code/1.0` both pass, so Relay's transport is fine as-is.
- The user's Kimi Code access is OAuth-only (device flow via the kimi CLI; no static key in `~/.kimi-code/config.toml`). Access tokens live ~15 min (`expires_in: 900`). The OAuth token works verbatim as Relay's Bearer key.
- Headless refresh works: POST `https://auth.kimi.ai/api/oauth/token` with grant_type=refresh_token and the CLI's public client_id `17e5f671-d194-4dfb-9706-5516cb48c098`, UA `kimi-code/1.0`.
- Gap: `session_protocol.py` looks the key up once at `set_model`/`configure`; a running pane keeps the stale token and 401s after ~15 min.

Interim workaround installed on the owner's machine: `~/.local/bin/kimi-code-relay-key.py` + systemd user timer `kimi-code-relay-key.timer` (every 5 min) refreshes the token and stores it in the keyring (`service org.relayterminal.Relay, provider kimi-code`). Verified: `keytest.check('kimi-code')` ok, and a `sidecall` with model `kimi-for-coding` replies through Relay's own provider path.

Proper fix options: (a) `keystore.lookup("kimi-code")` reads `~/.kimi-code/credentials/*.json` and refreshes on demand (still only helps at set_model time); (b) per-turn key re-resolution for presets marked oauth-style; (c) a small token-refresh transport wrapper for the kimi-code preset. Also consider naming `kimi-for-coding` "K2.8 Preview" in the catalog, as the kimi CLI does.

## Decisions
- "Hold, fix #9R2V first" (2026-09-21): no user-facing documentation or promotion of the kimi-code/K2.8 setup until per-turn token refresh is built; docs land in the same change. The owner's own machine keeps the systemd-timer workaround meanwhile.
