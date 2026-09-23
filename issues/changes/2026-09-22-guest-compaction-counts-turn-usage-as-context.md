---
id: CP3M
type: work
status: needs-verification
labels: [bug, context, guests]
assignee: agent
implemented_by: kimi/kimi-k3
session: 43a3f0e7-f04a-4981-b2dd-ee0432e64f24
rank: m
created: '2026-09-22'
source: Codex in a Relay pane, 2026-09-22
links: {plans: [], commits: [b469da5728573988afe4d7f68063ccd768ab0f71], evidence: [docs/qa_evidence/2026-09-23-cp3m/], related: [C8WX], github: null}
---
# Guest aggregate usage triggers spurious compaction and inflated token savings

## Issue
can you check how compaction works. 

i just saw (in this  convo e0711a2a) 

Conversation compacted (auto) · 3.1M → 54.3k tokens

could that be right? that seems like a bug in the numbers

## Execution Summary
Investigation confirmed the accounting bug; implementation remains open.

Pane e0711a2a maps to Relay session 678e5e19bfda407591a623f02e9421f6 and Codex thread 01a0ca52-9e46-74f1-beb5-95c16515d325. The GUI log records compaction_started and compacted at the same millisecond, 2026-09-23T00:40:13.679Z. The saved Relay session has epoch 0 and no summary boundary.

The Codex rollout's last usage before the preceding turn was 9,736,903 cumulative tokens (2026-09-22T19:15:49.130Z). At that turn's end (2026-09-23T00:24:21.647Z), it was 12,842,368: a delta of **3,105,465** tokens. The last individual request used **181,959** tokens in a **258,400** token window. Thus 3.1M is the aggregate across calls, not the context size.

`guest_harness_codex.CodexHarness._usage_for` intentionally emits the turn usage delta for accounting and a separate context_tokens measurement. `guest_harness_provider.relay_usage` maps the aggregate to total_tokens. `Agent.ask` passes it to `ContextTracker.record_usage`, which treats it as one request and also inflates its estimate calibration (up to 4x). `_maybe_compact` uses that inflated count when a separate summaries role is available. `Agent.compact` then invalidates the measurement and reports an estimate as the after count, even if `context.compact` returns unchanged messages. The 54.3k is therefore a Relay transcript estimate, not evidence that Codex's live context shrank to 54.3k. Its exact value was not independently reconstructed.

Local evidence: ~/.local/share/relay/logs/relay.log, ~/.local/share/relay/logs/worker.log, ~/.local/share/relay/sessions/83e6ce670835f99d/678e5e19bfda407591a623f02e9421f6.json, and ~/.codex/sessions/2026/09/22/rollout-2026-09-22T14-13-27-01a0ca52-9e46-74f1-beb5-95c16515d325.jsonl. Only usage metadata was extracted from the private rollout.

Fix landed in b469da57 (2026-09-23), three changes in `backend/relay_core/agent.py` plus tests:

1. The turn loop skips `ContextTracker.record_usage` when the turn is served by a guest harness (new `_guest_harness()` predicate, also used by `_maybe_compact`). A guest's usage event is the turn's aggregate — every request it made, summed — so it can no longer serve as the tracker's single-call measurement or calibrate the estimate ratio. Session totals (`sessions.add_usage` in `_provider_emit`) still count guest tokens, so usage accounting is unchanged.
2. `Agent.compact` no longer invalidates the tracker when the transcript was not rewritten (`boundary is None and trimmed == 0`): it reports `after == before` instead of switching measurement basis and claiming a reduction that never happened. The title/summary staleness flags are only set when the conversation was actually rewritten.
3. `tests/test_agent.py` gains `GuestAggregateUsageTests`: a guest fake emitting a 3.1M aggregate leaves the tracker unrecorded and the ratio at 1.0 while the session totals reach 3,105,465; a Relay provider's per-call usage still records; an unchanged one-turn manual compaction reports before == after and keeps the usage measurement.

Evidence: `docs/qa_evidence/2026-09-23-cp3m/tests.md`. One unrelated pre-existing failure surfaced in the full run (`WorkerTests.test_malformed_request_does_not_crash`, worker.py:763 bare `kind` on a malformed message, present on HEAD) — filed as #1Q2F, untouched here.

## Done means
Guest turn totals continue to count toward usage, but never serve as a single-request context measurement or calibrate Relay's transcript estimate. Compaction decisions and before/after counts use a consistent basis. An unchanged transcript does not claim a reduction caused solely by changing accounting methods. Guest context remains distinct from Relay's retained transcript.

## Tests
`XDG_DATA_HOME=$(mktemp -d) RELAY_KEYRING=off PYTHONPATH=backend python3 -m unittest tests.test_agent.GuestAggregateUsageTests -v`
`XDG_DATA_HOME=$(mktemp -d) RELAY_KEYRING=off PYTHONPATH=backend python3 -m unittest tests.test_agent tests.test_guest_context_meter tests.test_guest_harness_provider`
manual: docs/qa_evidence/2026-09-23-cp3m/tests.md

## Plan
**Goal:** guest turn-aggregate usage stops serving as a single-request context measurement, and a compaction that changes nothing reports no reduction.

**Findings:** `backend/relay_core/agent.py` — turn loop records `_last_usage` into `ContextTracker.record_usage` (~line 2148) for any provider, including guest harnesses whose `relay_usage` maps a *turn-cumulative* delta to `total_tokens` (`backend/relay_core/guest_harness_provider.py` `relay_usage`, codex side `_usage_for`). `Agent.compact` (~line 1873) always calls `context.invalidate()` and re-measures, so an unchanged transcript reports before (usage-based) ≠ after (estimate). `context.compact` (`backend/relay_core/context.py:310`) signals "nothing rewritten" as `boundary is None and trimmed == 0`. The guest predicate already exists inline at `_maybe_compact` (agent.py:1936): `_injected_provider and not getattr(provider, "serves_side_calls", True)`.

**Steps:**
1. agent.py: add `_guest_harness()` helper; use it in `_maybe_compact` and skip `context.record_usage` for guest harness usage in the turn loop. Session totals (`sessions_usage.add_usage` in `_provider_emit`) are untouched — guest tokens still count toward usage.
2. agent.py `compact()`: when `boundary is None and not result["trimmed"]` (transcript unchanged), keep the tracker state and report `after = before` instead of invalidating and re-estimating; skip the title/summary staleness flags in that case.
3. tests/test_agent.py: (a) a guest fake (`serves_side_calls = False`) emitting a multi-million-token aggregate leaves `ContextTracker` unrecorded and ratio uncalibrated; (b) `compact("manual")` on a one-turn conversation with recorded usage reports before == after.

**Risks:** none user-facing to decide; guest panes' context chip already reads `guest_context` (C8WX), so skipping the tracker does not darken any number the pane shows — Relay's own `context` event for guests falls back to a transcript estimate, as before invalidation.

**Verify:** `python3 -m pytest tests/test_agent.py -k compact -q` plus the full `tests/test_agent.py` and `tests/test_guest_context_meter.py` runs.
