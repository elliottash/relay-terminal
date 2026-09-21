---
id: DC4J
type: work
status: needs-verification
assignee: agent
implemented_by: kimi/kimi-k3
session: 70a224e1-0f04-41bc-aa56-9168c8aeb723
rank: zzzzzzzzzzzzzzzi
created: '2026-09-20'
links: {plans: [], commits: [6dceb819], evidence: [docs/qa_evidence/2026-09-21-dc4j-swap-hint/], related: [], github: null}
---
# changing models during rate limit retries -- bug

## Issue
there is a bug  i think, where when a model gets a denial eg 429 and starts retrying, i cant switch the model. if i click a new one in the picker, it keeps retrying rather than move immediately. i have to esc to escape and interrupt the retries. that shouldnt happen -- it should just switch over.

too help with this, add /swap as a command that immediately swaps to your default fallback (or back to your first choice provider if you are on the fallback).

## Plan
**Goal** — A model switch from the picker (or `/swap`) during the retry wait of a refused request (429/5xx) takes effect at once instead of waiting out every `Retry-After`; `/swap` toggles between the main model (priority rank 1) and the fallback (rank 2).

**Findings** — Both halves already exist in the working tree, citing card #DC4J; the card was never updated. Audit first, change only what is missing:

- *Retry preemption (the bug).* `backend/relay_core/provider.py`: `ProviderPreempted` (line ~492), `ChatProvider.preempt_check` hook asked every `RETRY_WAIT_TICK = 0.1` s during a retry wait (lines ~645, ~977, ~1032). `backend/relay_core/agent.py`: `apply_pending_model`/`_switch_waiting` gate (lines ~1119–1132, ~1204–1249, ~1885) and `_note_preempted_retry`, which emits `provider_retry {reason: "switch", status, attempt, from_model, to_model}` before the `model_applied` (lines ~2763–2779). Spec updated in `docs/AGENT-SESSIONS-PROTOCOL.md` (sections at lines 42, 644, 1838). Tests: `RetryPreemptionTests` in `tests/test_model_switch.py` cover both the at-once switch during a 429 wait and the streaming case that must still wait for the step boundary.
- *`/swap`.* Registered in the slash list at `src/Pane.h:7931` and handled at `src/Pane.h:8279–8300`: swaps to the rank-2 fallback or back to rank 1, skips exhausted subscriptions, goes through `selectEntry` like any pick (`src/Pane.h:1326`). Ranks come from `models/priority` (`src/ModelCatalog.h:13`, `src/ModelCatalog.cpp` `curation::fallback`). `docs/ARCHITECTURE.md:1888` documents it.
- *Gap found:* the standing shortcut-hints rule — no hint teaches `/swap`. When the user switches to the fallback (or back to main) the slow way (model box or picker, i.e. `selectEntry` reaching a rank-1/rank-2 swap from the mouse), show a `relay::ShortcutHints::nextTime("/swap", …)` hint, as the other `*.slash` hints in `src/Pane.h` do (`/model` already has one at `src/Pane.h:11362`).

**Steps**

1. `git log --oneline --grep=DC4J` and `git status`: confirm whether the retry-preemption and `/swap` changes are committed, in progress under a `scripts/land.py begin` hold, or unclaimed. If another session's `land.py` hold covers these files, stop and report on the card instead of touching them.
2. Read `backend/relay_core/provider.py` (preempt path) and `backend/relay_core/agent.py` (`_switch_waiting`, `_note_preempted_retry`) against `docs/AGENT-SESSIONS-PROTOCOL.md` section 2: the switch must only preempt a *wait* between attempts, never a request that has started streaming (that rule is covered by `test_a_switch_while_the_reply_is_streaming_still_waits_and_says_so`). Fix anything that diverges from the spec.
3. Add the `/swap` shortcut hint in `src/Pane.h` where `selectEntry` applies a pick that lands on rank 1 or rank 2 from a non-`/swap` path: id like `model.swap.slash`, text via `relay::ShortcutHints::nextTime(QStringLiteral("/swap"), QStringLiteral("swapping main ↔ fallback"))`, through the existing `hint(id, …)` gate. Keep it to one line plus comment, per the standing rule.
4. Land per the project rule: `python3 scripts/land.py begin <me> <paths>` before any edit that is still needed, `python3 scripts/land.py commit <me> -m …` after; the card moves with the commit.

**Risks**

- The code may be another session's uncommitted work. Step 1 decides; do not clobber a `land.py` hold.
- `preempt_check` fires inside the provider's retry sleep; a buggy gate could abort a request that is actually streaming. The streaming test must keep passing unchanged.
- `/swap` while a failover is in progress (`provider_retry {reason: "failover"}`) targets the same ranked list; confirm the handler does not fight the failover chain (`src/Pane.h:1367` rebuilds it on `models/fallback` changes).

**Verify**

- `python3 -m pytest tests/test_model_switch.py -k RetryPreemption` (both tests) plus the rest of `tests/test_model_switch.py`.
- Build via `scripts/relay-build` after the hint change; run Relay under Xvfb with an isolated `XDG_CONFIG_HOME`, point a pane at an endpoint answering 429 with a long `Retry-After`, switch models in the picker mid-wait and confirm the switch is immediate, then `/swap` twice and confirm it toggles main ↔ fallback with the status line naming the target.

## Execution Summary
The audit (plan steps 1–2) found both halves already landed and correct against the protocol spec — retry preemption in `7824689d`, `/swap` in `c7dccfa0` — so the only code change was the gap the plan named:

- `src/Pane.h`: new `hintSwapForPick(key)` beside `openModelPicker`, called from the two mouse pick paths (the model box `entry:` row in `modelBoxPicked`, and the picker in `openModelPicker`) before `selectEntry` moves the pane. A pick that swaps the ranked Main (rank 1) for its fallback (rank 2), or back, shows the `model.swap.slash` hint — "Next time: /swap … swapping main ↔ fallback" — through the existing `hint(id, …)` gate. `/swap` itself and a typed `/model <name>` never trigger it.
- Landed as `6dceb819` (land.py verified the exact tree builds). Evidence: `docs/qa_evidence/2026-09-21-dc4j-swap-hint/README.md` — the audit findings, the test run and the build log.
- Land mechanics note: a second `begin` after editing re-snapshotted `src/Pane.h`; the pre-edit snapshot was restored by reversing the two known edits, and the commit review confirmed the only hunks were this change's (+15 −0).

## Tests
`python3 -m unittest tests.test_model_switch.RetryPreemptionTests` — both OK; full module `tests.test_model_switch` 19/19 OK
`python3 -m unittest tests.test_provider` — 69/69 OK
`ctest --test-dir build -R modelpicker|helpermodelbox|modelrows|modelcatalog` — 4/4 passed
`scripts/relay-build` — built in 49s; land.py's verify step rebuilt the exact landed tree `6dceb819`
manual: docs/qa_evidence/2026-09-21-dc4j-swap-hint/

## QA checklist
- [ ] Live 429 run: Relay under Xvfb with an isolated `XDG_CONFIG_HOME`, a pane pointed at an endpoint answering 429 with a long `Retry-After`; switch models in the picker mid-wait and confirm the switch is immediate (status line names the new model, `provider_retry {reason: "switch"}` in the transcript), with no Esc needed.
- [ ] `/swap` twice in a pane with two ranked models: toggles main ↔ fallback, status line names the target ("Swapped to … (the fallback)" / "(the main model)"); from an exhausted model it goes to the first live one.
- [ ] Hint: with two ranked models, pick the fallback in the model box (mouse) while on Main — the `model.swap.slash` hint ("Next time: /swap …") appears; repeat from the picker. Confirm it does *not* appear after a typed `/swap` or `/model <name>`, and respects the per-hint show limit.
- [ ] Streaming rule unchanged: start a reply on a slow model, switch models mid-stream — the reply finishes on the old model and the status line says the new one takes over from the next step.
