---
id: RLP7
type: work
status: discussing
labels: [feature, models, gateway]
assignee: codex
waiting_on: owner
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

## Planning notes
Code review, 2026-09-22 (Codex): `gateway/server.py` authenticates an installation token and then
accepts any configured role; `Installation.plan` is returned to clients but does not gate roles.
Adding Pro roles to the current role table alone would expose them to every Free installation.
Pro therefore needs a server-side entitlement check before upstream selection. The existing
identity/token exchange in `backend/relay_core/hosted.py` can remain the installation identity.
The access code must be stored through the keyring and omitted from logs and public preset rows.
`gateway/config.py` owns upstream models, prices and reasoning ceilings; the desktop should keep
abstract model IDs rather than duplicate that mapping.

## Done means
Pending the owner's product decisions: a valid code enables the selected Pro models; missing,
invalid or revoked codes cannot call Pro, including through a modified client. Free and BYOK
continue working. The provider row reflects the gateway's entitlement result, and the selected
lite policy is tested. No upstream credentials appear in the desktop or evidence.

## Plan
**Goal:** password-enabled hosted Pro, scoped by the owner's answers above.
**Findings:** gateway auth/role routing is in `gateway/server.py`, configuration in
`gateway/config.py`, installation/token storage in `gateway/store.py`, hosted desktop access in
`backend/relay_core/hosted.py` and `provider.py`, provider UI in `src/RelayWindow.h`.
**Steps:**
1. Record access-code scope, upstream roles and lite policy as decisions.
2. Add server-side code validation/revocation and Pro-role authorization, with code digests
   stored server-side and unchanged Free behavior.
3. Add desktop keyring storage, entitlement validation and the Pro provider row; preserve
   code secrecy in worker events, settings and logs.
4. Integrate the chosen Pro defaults/levels with `presets.py`, `model-ranking.md` and role resolution.
5. Exercise valid/invalid/revoked access, Free regression, quota and streaming paths against a
   local fake upstream, then drive provider activation/deactivation in isolated Relay.
**Risks:** exact upstreams, quotas and rollout remain unspecified. Live deployment is separate
from preparing and verifying code; no production configuration has been changed.
**Verify:** targeted gateway/hosted/provider tests and live isolated GUI evidence, including a
forged Pro request without entitlement and revocation of an already-issued session token.
