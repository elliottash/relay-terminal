---
id: RLP7
type: work
status: inbox
labels: [feature, models, gateway]
assignee: unassigned
rank: n
created: '2026-09-21'
source: 'Claude Code in a Relay pane, 2026-09-21'
links: {plans: [], commits: [], evidence: [], related: [MDL1], github: null}
---
# Relay Pro: stronger hosted models behind a password

## Issue
remind me what are the high / main / flash models in relay free? i want to also plan a relay pro that i can enable with a password

## Discussion points
Relay Free is three abstract models (`relay-main`, `relay-flash`, `relay-lite`) that the gateway on
elliott-main-1 maps to upstreams (`docs/RELAY-FREE.md`); there is no separate high model, since high
runs `relay-main` at its top level. Pro fits as a second entitlement on the same gateway rather than
a second service. Claude's proposal, not yet ruled on:

- a `relay-pro` preset with the same three roles, mapped by the gateway to stronger upstreams, with
  higher output caps and a real max level;
- an access code in the models pane's providers tab, sent with each request and checked against a
  list on the box; the row is usable only while the gateway says the code is good;
- `relay-pro` rows in `backend/relay_core/model-ranking.md`, so the defaults treat it like any
  provider once enabled.

Questions for the owner:
1. One shared code, or one per person (which is what revoking one later needs)?
2. Which upstreams should Pro serve?
3. Does Pro replace Relay Free in the lite rule, or sit above it?
