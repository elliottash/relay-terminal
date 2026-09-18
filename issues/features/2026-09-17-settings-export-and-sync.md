---
id: 05J2
type: work
status: ready
labels: [feature]
component: [gui, worker]
milestone: desktop-alpha
workstream: terminal
rank: zz05
created: '2026-09-17'
acceptance: settings export to a file and import on another machine; a design for optional encrypted sync is recorded
source: '`issues/feature_intake.txt`, 2026-09-17: "no accounts, but make it where you can export your settings or otherwise make it easy to share across computers, maybe with a brave-like sync system."'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Export settings, and an optional sync across machines

No accounts. Export and import everything that is not a secret: keymap, theme, model roles (not keys),
Agent options, Switchboard preferences, aliases. Then consider a Brave-style sync: a device pairs with a code,
content encrypted end to end, no account, using the same pairing machinery as remote access (#W5N2).

That machinery now exists and was deliberately factored to be reused (2026-09-18): `remote/pairing.py`
is pairing on its own — the QR link with the secret in the fragment, single-use short-lived rooms,
and the five-digit code both ends derive from the handshake — with no knowledge of panes or agents.
`remote/noise.py` and `remote/identity.py` hold the session and the pinned device keys. Sync would
add its own payload over that, not a second way to pair.
API keys stay in the keyring and are never exported unless the user explicitly asks.
