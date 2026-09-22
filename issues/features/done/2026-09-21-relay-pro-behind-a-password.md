---
id: MKZ0
aliases: [RLP7, RPR7]
type: work
status: done
labels: [feature, models, gateway]
assignee: codex
rank: n
created: '2026-09-21'
source: 'Claude Code in a Relay pane, 2026-09-21'
links: {plans: [], commits: [06d0936a902c14f860471233e3af2ce8973a7e22, 1cf0f5f68778d42fa0ddb4114acc44ccf42dffe4, 380d8c66c8f9ada774186901891393ab96566898, 6668999edd9c917790c473e08c72671087a82936, cdaf887e4611c14ab618c8a2d6cf6bba01f80e76, f8937979f543745d5764c54a35eaec1244f23a6a, eb2a2671b315e6711c0c6b36052e890ea172c6da, 0263794b483e0a7f593d93785f2e5f91510a9f44, 82e164cc17d41de7ef153cf30964a7da89ac3eef, 50c23e65ba48f3b37d9eab33e64d56bada680278], evidence: [docs/qa_evidence/2026-09-22-RPR7/README.md, docs/qa_evidence/2026-09-22-verify-RPR7/README.md], related: [4BPE], github: null}
---
# Relay Pro: GLM models with personal access codes

## Issue
remind me what are the high / main / flash models in relay free? i want to also plan a relay pro that i can enable with a password

## Decisions
> 1 pro - per person
> 2 i think there should be two versions actually.
>
> relay pro could be glm 5.3 (high / main), glm 5.3 flash (flash)
>
> relay ultra could be astra or fable -- lets defer that

Implementation: per-person revocable codes; Pro high/main use GLM 5.3 and flash uses GLM 5.3 Flash. Ultra is explicitly deferred. Lite retains Relay Free (the prior recommendation; no Pro lite was requested). Keep current quota/spend ceilings unless configured otherwise; no billing or production deployment in this change.

## Discussion points
Relay Free is three abstract models (`relay-main`, `relay-flash`, `relay-lite`) that the gateway on
elliott-main-1 maps to upstreams (`docs/RELAY-FREE.md`); there is no separate high model, since high
runs `relay-main` at its top level. Pro fits as a second entitlement on the same gateway rather than
a second service. Initial proposal (resolved in Decisions above):

- a `relay-pro` preset with the same three roles, mapped by the gateway to stronger upstreams, with
  higher output caps and a real max level;
- an access code in the models pane's providers tab, sent with each request and checked against a
  list on the box; the row is usable only while the gateway says the code is good;
- `relay-pro` rows in `backend/relay_core/model-ranking.md`, so the defaults treat it like any
  provider once enabled.

Original questions, resolved above:
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
A valid per-person code enables Pro high/main (GLM 5.3) and flash (GLM 5.3 Flash); missing,
invalid or revoked codes cannot call Pro, including through a modified client. Free and BYOK
continue working. The provider row reflects the gateway's entitlement result, and the selected
lite policy is tested. No upstream credentials appear in the desktop or evidence.

## Plan
**Goal:** per-person code access to GLM 5.3 high/main and GLM 5.3 Flash; Ultra deferred, lite remains Free.
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
**Risks:** operator credentials/prices and production rollout are deployment configuration; current quota and spend ceilings remain. Live deployment is separate
from preparing and verifying code; no production configuration has been changed.
**Verify:** targeted gateway/hosted/provider tests and live isolated GUI evidence, including a
forged Pro request without entitlement and revocation of an already-issued session token.

## Tasks
- [x] Record owner decisions; defer Ultra and retain Free lite. <!-- t:p1 -->
- [x] Add digest-only per-person code issuance, revocation and request authorization. <!-- t:p2 -->
- [x] Add background access validation, keyring code controls and current-code availability. <!-- t:p3 -->
- [x] Add GLM 5.3 high/main, GLM 5.3 Flash and adjustable low/medium/high effort. <!-- t:p4 -->
- [x] Exercise live activation, model use, revocation and removal; run targeted regressions. <!-- t:p5 -->

## Execution Summary
Gateway authorization and operator CLI landed in `1cf0f5f6`; Pro is checked on every request,
including for already-issued installation tokens. Desktop access controls and usable-model gating
landed in `380d8c66`, `6668999e` and `cdaf887e`. Backend integration/defaults landed in `f8937979`;
`eb2a2671` rejects foreign endpoint overrides before any Pro code lookup, requires the complete
role set and distinguishes authorization from keyring errors. `0263794b` aligns advertised effort
levels with the gateway ceiling. Codes never enter public preset/configuration events or upstream
requests. Lite remains Free. Production rollout/configuration and code issuance remain operator
work; `gateway/README.md` documents the concrete procedure and required model-price inputs.

Evidence: `docs/qa_evidence/2026-09-22-RPR7/README.md`, including real local HTTP tests and a
running isolated Relay activation → Pro turn → revocation → removal drive. No live GLM inference
or production service changes are claimed.

## Tests
`tests/test_relay_pro.py`
`tests/test_gateway_pro.py`
`tests/test_gateway.py`
`tests/test_hosted.py`
`tests/test_presets.py`
`tests/test_roles.py`
`tests/test_model_ranking.py`
`tests/test_keytest.py`
`tests/test_session_protocol.py`
`ctest -R modelcatalog`
`ctest -R modelpicker`
`ctest -R modelsettings`
manual: docs/qa_evidence/2026-09-22-RPR7/README.md

### Check 2026-09-21 22:15
- passed · unittest:tests.test_relay_pro — tests/test_relay_pro.py passed for this revision on spark-dcc9, 2026-09-22T02:13:54Z
- passed · unittest:tests.test_gateway_pro — tests/test_gateway_pro.py passed for this revision on spark-dcc9, 2026-09-22T02:13:54Z
- passed · unittest:tests.test_gateway — tests/test_gateway.py passed for this revision on spark-dcc9, 2026-09-22T02:13:54Z
- passed · unittest:tests.test_hosted — tests/test_hosted.py passed for this revision on spark-dcc9, 2026-09-22T02:13:54Z
- passed · unittest:tests.test_presets — tests/test_presets.py passed for this revision on spark-dcc9, 2026-09-22T02:13:54Z
- passed · unittest:tests.test_roles — tests/test_roles.py passed for this revision on spark-dcc9, 2026-09-22T02:15:37Z
- passed · unittest:tests.test_model_ranking — tests/test_model_ranking.py passed for this revision on spark-dcc9, 2026-09-22T02:15:37Z
- passed · unittest:tests.test_keytest — tests/test_keytest.py passed for this revision on spark-dcc9, 2026-09-22T02:15:37Z
- passed · unittest:tests.test_session_protocol — tests/test_session_protocol.py passed for this revision on spark-dcc9, 2026-09-22T02:15:37Z
- passed · ctest:modelcatalog — ctest -R modelcatalog passed for this revision on spark-dcc9, 2026-09-22T02:15:24Z
- passed · ctest:modelpicker — ctest -R modelpicker passed for this revision on spark-dcc9, 2026-09-22T02:15:24Z
- passed · ctest:modelsettings — ctest -R modelsettings passed for this revision on spark-dcc9, 2026-09-22T02:15:24Z
- not-applicable · manual:docs/qa_evidence/2026-09-22-RPR7/README.md — manual evidence, recorded by hand: docs/qa_evidence/2026-09-22-RPR7/README.md
- notice · unittest:tests.test_session_protocol — tests/test_session_protocol.py: 7 of 35 are slow (test_the_row_moves_while_nobody_is_typing, test_two_claudes_in_one_directory_tail_their_own_sessions, test_indexing_the_guests_off_means_no_tail…)
- notice · ctest:modelcatalog — ctest -R modelcatalog is slow: p95 3.72 s, p50 3.72 s
- notice · ctest:modelpicker — ctest -R modelpicker is slow: p95 1.78 s, p50 1.78 s
history: thread

## QA checklist
Independent Relay verifier a5 checked these against Done means; full record in
`docs/qa_evidence/2026-09-22-verify-RPR7/README.md`.
- [x] Personal activation enables GLM 5.3 high/main and GLM 5.3 Flash; a live Pro turn succeeds. <!-- t:v1 -->
- [x] Missing, invalid, forged and revoked access are denied; revocation blocks an existing live session before upstream traffic. <!-- t:v2 -->
- [x] Free and BYOK still work; lite defaults and chores remain Free. <!-- t:v3 -->
- [x] Provider availability follows access; stale/changed codes cannot reactivate it. <!-- t:v4 -->
- [x] Codes remain out of public configuration, logs and upstream requests; foreign endpoint overrides are rejected. <!-- t:v5 -->

## Verdict
PASS — independent verifier a5 reran 22 focused tests, an additional BYOK probe, and a separate
actual Relay activation → Pro turn → revocation drive. Upstream count remained 3 before and
after the refused turn. Evidence committed in `50c23e65`. Root reviewed the evidence and closes
the implementation as done. Production rollout and live GLM inference are outside this verdict;
operator configuration and code issuance are documented in `gateway/README.md`.
