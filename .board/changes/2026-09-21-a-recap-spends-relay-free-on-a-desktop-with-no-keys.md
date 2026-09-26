---
id: RCPF
type: work
status: needs-verification
labels: [feature, remote]
component: [worker]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 4386a69e-3006-48f5-bb9c-6e7338a8c7df
rank: m
created: '2026-09-21'
source: 'Claude Code session on #PH0N, 2026-09-21: finding 8 of the hosted drive, docs/qa_evidence/2026-09-21-ph0n-hosted-drive/'
links: {plans: [], commits: [15ee6c5], evidence: [docs/qa_evidence/2026-09-21-ph0n-hosted-drive/, docs/qa_evidence/2026-09-25-rcpf-hosted-off/], related: [PH0N, HG7K, WMXN]}
---
# A recap on a desktop with no keys is written by Relay Free, unasked

## Issue

Found by #PH0N's hosted drive. A recap's side call asks for a cheap model
(`_start_recap` → `agent.side_provider(cheap=True, role="summaries")`,
`backend/relay_core/session_protocol.py:481`). On a profile with no Flash or Lite model
configured that falls through to Relay Free, so run 3 of the drive printed a recap written by a
hosted model on a machine started with `RELAY_KEYRING=off` and a pane on a local model. The same
shape hits any fresh profile, which is why QA drives keep reaching the network by accident
(`docs/qa_evidence/2026-09-20-perf-profile` saw it too).

Two of the three recap reasons are automatic (`away`, `resume`), so this can spend the allowance
with nobody asking for anything.

## Discussion points

The question is the owner's, because Relay Free exists precisely so that a keyless install still
works (#HG7K), and a recap is exactly the kind of small chore it was meant for.

Options, with a recommendation:

1. **Leave it.** Relay Free is the fallback for every model call; a recap is not special.
2. **Recommended: a manual recap may use Relay Free; an automatic one (`away`, `resume`) does
   not.** The person asked for the first and not the second, and an away recap that silently
   spends quota is the case that surprises. An automatic recap with no local or BYOK cheap model
   is skipped, and says so in the pane in one line.
3. **Never for side calls.** Titles, summaries and recaps stay on a configured model or do not
   happen. Cleanest for spend, worst for a fresh install, where the pane would fill with
   "no model for this".

Whichever is chosen, a test profile should be able to say "no hosted fallback" in one setting, so
a drive cannot reach the network by forgetting a tier list.

## Done means
Per the owner's decision on #PH0N, a recap may still spend Relay Free — that behaviour does not change. What this card adds is the one switch from the Discussion's last line: `RELAY_HOSTED=off` in the environment makes a profile unable to reach Relay's hosted gateway at all — no pane turn, recap, title, summary, key test or quota fetch registers or calls it — and a side role that would have used Relay Free falls back to the pane's own model instead. `scripts/relay-qa-run` sets it, so a QA drive is offline-by-default. Failure looks like: with the switch set, any code path still reaches the gateway (the gateway's log shows a request), or `hosted.available()` still answers True; or, with the switch unset, ordinary Relay Free use regresses.

## Plan
**Goal.** The card's question is answered (owner, 2026-09-21: a recap on a keyless desktop may spend Relay Free), so no recap behaviour changes. Build the tail of the Discussion: one environment switch, `RELAY_HOSTED=off`, that makes Relay Free unusable in a profile, and set it in the QA-run harness so a drive cannot reach the network by forgetting a tier list.

**Findings.**

- Recap path: `SessionCommands._start_recap` (`backend/relay_core/session_protocol.py:551`, called for `resume` at :543 and `recap_request` at :549) asks `agent.side_provider(cheap=True, role="summaries")`, then `suggestions.recap` runs it. `side_provider` (`backend/relay_core/agent.py:1821`) resolves through `RoleResolver` (`backend/relay_core/roles.py`): `summaries` is a `BACKGROUND_ROLE` on the flash tier; with no flash tier list or built-in default it steps towards Main, and on a fresh/keyless profile Main is Relay Free. `RoleResolver.has_key` (`roles.py:686`) counts Relay Free as usable whenever `hosted.available()`.
- `hosted.available()` (`backend/relay_core/hosted.py:147`) today only checks that the cryptography imports exist — it never considers a user switch. The actual network touch happens later, in `Session.token()` (`hosted.py:323`, registers an installation key with the gateway) via `hosted.session()` (`hosted.py:466`). `hosted.status()` feeds the presets event the GUI uses for its Relay Free row.
- Existing env knobs are `RELAY_KEYRING=off` (keystore) and `RELAY_HOSTED_URL` (`hosted.py:42`, repoints the gateway for tests). Nothing disables hosted outright. QA drives export `RELAY_KEYRING=off` but can still reach Relay Free — exactly the accidents the card cites. The QA harness is `scripts/relay-qa-run` (described in `docs/DEBUG-HYGIENE.md:42`): it already builds the isolated env.

**Steps.**

1. `backend/relay_core/hosted.py`: add `ENV_OFF = "RELAY_HOSTED"` next to `ENV_URL`, a small `disabled()` helper (value `off`/`0`/`no`, matching the `RELAY_KEYRING=off` convention), and gate three places: `available()` returns False when disabled; `Session.token()` raises `HostedUnavailable` naming the switch, so even a caller that skips `available()` never registers with the gateway; `status()` reports Relay Free unavailable (so the GUI row greys out the same way it does for a missing dependency).
2. `scripts/relay-qa-run`: export `RELAY_HOSTED=off` alongside `RELAY_KEYRING=off`, with one comment line saying drives that exercise Relay Free (like #PH0N's hosted drive) unset it on purpose. Update the description in `docs/DEBUG-HYGIENE.md`.
3. Docs: add `RELAY_HOSTED=off` to the environment-variable list in `docs/AGENT-SESSIONS-PROTOCOL.md` (next to the `RELAY_KEYRING=off` entry around :1257): one setting, no hosted fallback, for test profiles.
4. Tests:
   - `tests/test_hosted.py`: with `RELAY_HOSTED=off`, `available()` is False, `status()` reports unavailable, and `session().token()` raises `HostedUnavailable` without touching the network (the existing `test_env_no_keyring`-style env patches at :209 are the pattern; keep the passing case at :526 proving unset still works).
   - `tests/test_roles.py` or `tests/test_tier_lists.py` (whichever already builds a RoleResolver over a fake keystore): on a fresh/keyless profile with `RELAY_HOSTED=off`, the `summaries` role resolves to the pane's own model, never to a `relay-free` config; with it unset the current fall-through still happens (guards against the switch breaking normal Relay Free).

**Risks.** A pane whose main model *is* Relay Free with the switch set errors on every turn with the `HostedUnavailable` message — acceptable: the switch is for test profiles, and the message says why. Env naming follows `RELAY_KEYRING=off` (`RELAY_HOSTED=off`, not a new `RELAY_NO_*` shape) unless the executor finds a conflicting existing use of `RELAY_HOSTED`. No GUI work beyond what `status()` already feeds; if the Relay Free row needs more than greying to stay honest, say so on the card rather than growing this.

**Verify.** `python3 -m unittest tests.test_hosted tests.test_roles tests.test_tier_lists` (whichever of the last two the new test lands in). Manual: run `scripts/relay-qa-run` (or `RELAY_KEYRING=off RELAY_HOSTED=off` with a fresh XDG profile) with a pane on a loopback local stub, idle past the away-recap threshold, and confirm the recap is written by the local stub and the gateway was never contacted (no token file under the scratch `XDG_DATA_HOME`, stub log shows the recap request).

## Tests
- `tests/test_hosted.py::TransportTests::test_relay_hosted_off_reaches_the_gateway_by_no_path`: `off`/`0`/`no`/`OFF` all disable; `available()` False; `status()` unavailable; `token()`, `token(force)`, `fetch_pro`, `image`, `_post` and a hosted chat turn all refuse naming `RELAY_HOSTED=off`; `image_roles()` is `[]`; the fake gateway records **zero** requests; unset again, the same session registers.
- `tests/test_tier_lists.py::…::test_relay_hosted_off_keeps_a_harness_panes_background_jobs_off_relay_free`: with the real `hosted.available()`, a harness pane's `summaries`/`chores`/`terminal_use` stay on the pane's model when off, and `summaries` still falls through to `relay-free` when unset.
- `tests/test_relay_pro.py::ProClientTests::test_relay_hosted_off_names_the_switch_and_reaches_nothing`.
- `HostedCase` and `ProClientTests` now clear `RELAY_HOSTED` so both suites pass inside a `relay-qa-run` shell (checked with `RELAY_HOSTED=off` exported).
- Run: `PYTHONPATH=backend python3 -m unittest tests.test_hosted tests.test_relay_pro tests.test_roles tests.test_tier_lists`. Everything passes except two failures that are **also on a clean `git archive HEAD` export**, so they predate this card: `test_tier_lists.StartEffortTests.test_every_cloud_row_carries_both_keys` (3 relay-pro effort subtests) and `test_hosted.WorkerTests.test_configure_relay_free_and_ask_streams_a_reply_and_the_quota` (flaky `FREE_OKFREE_OK`, 3/4 runs at HEAD).
- Live drive: `docs/qa_evidence/2026-09-25-rcpf-hosted-off/drive-output.txt`. Under `scripts/relay-qa-run` the logging loopback stub saw **no request**; with `RELAY_HOSTED=` it saw `/v1/challenge` and `/v1/health`.

## Execution Summary
Landed in `15ee6c5`. `hosted.disabled()` reads `RELAY_HOSTED` (`off`/`0`/`no`/`false`). Beyond the plan's three gates (`available()`, `token()`, `status()`), the gate also covers both transport exchanges (`_exchange`, `_exchange_image`) and `image_roles()`. The plan missed that last one: it makes an unauthenticated `GET /v1/health`. With those gated, nothing this module opens can reach the gateway. `relay_pro.validate` names the switch instead of "could not be checked. Try again". `scripts/relay-qa-run` uses `RELAY_HOSTED="${RELAY_HOSTED-off}"`, so it is off by default and `RELAY_HOSTED= scripts/relay-qa-run …` opts a Relay Free drive back in. Documented in `docs/DEBUG-HYGIENE.md` and next to `RELAY_KEYRING=off` in `docs/AGENT-SESSIONS-PROTOCOL.md`.

Finding: a plain local-model pane already keeps `summaries` on its own model. The fall-through to Relay Free happens for a harness pane (and a Relay Free main), so the resolver test uses that shape.

Not done: the GUI idle-past-away-recap drive from the plan's Verify. The recap's only hosted touch is `side_provider(role="summaries")`, which goes to the resolver and then the hosted transport. The live stub drive exercises both, so the drive would add a screenshot rather than coverage.
