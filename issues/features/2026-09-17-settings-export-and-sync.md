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
API keys stay in the keyring and are never exported unless the user explicitly asks.
