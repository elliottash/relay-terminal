---
id: 31BM
type: work
status: needs-verification
labels: [feature, context]
assignee: agent
implemented_by: glm/glm-5.3
session: 35973851-11b2-4781-a133-754b6eee2b76
rank: zzzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, criteria: 'tests/test_compaction_progress.py passes: compaction_progress events carry growing chars and a positive estimate between compaction_started and compacted', sign_off: none, effort: low, stakes: nuisance}
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-24-compaction-progress/], related: [], github: null}
---
# % progress indicator on compaction

## Issue
is there a way to put a % progress indicator on compaction

## Done means
A compaction's summary call streams its deltas: between `compaction_started` and `compacted` the pane emits throttled `compaction_progress {chars, estimate, phase}` events, and the context chip shows `compacting… N%` (clamped at 95% until the call returns; `compacting… thinking` while reasoning deltas stream, plain `compacting…` during prefill). The estimate is the previous compaction's `summary_chars` when there was one, else `clamp(transcript chars / 12, 2000, 12000)`. Fake-provider test proves the events arrive in order with growing `chars`. `docs/AGENT-SESSIONS-PROTOCOL.md` documents the event.

## Execution Summary
Landed in `231b0918`. `sidecall.call` gained `on_delta` (it forwards the `delta`/`thinking_delta` events it used to swallow), `context.summarize` counts content chars against an estimate (previous compaction's `summary_chars`, else `clamp(transcript/12, 2000, 12000)`) and reports them as `on_progress(chars, estimate, thinking)`, and `Agent.compact` emits throttled `compaction_progress {chars, estimate, phase}` between `compaction_started` and `compacted`. The chip in `src/Pane.h`/`src/PaneSession.cpp` shows `compacting… N%` (clamped at 95% until `compacted`), `compacting… thinking` on reasoning deltas, plain `compacting…` during prefill. Evidence: `docs/qa_evidence/2026-09-24-compaction-progress/` — a driver run under Xvfb against a fake provider whose summary streams for 24 s, photographed mid-compaction.

![The context chip mid-compaction, showing compacting… 20% while the pending model switch waits](../../qa_evidence/2026-09-24-compaction-progress/03-compacting-percent.png)

![Ten seconds later in the same stream: compacting… 38%](../../qa_evidence/2026-09-24-compaction-progress/04-compacting-later.png)

## Tests
- `PYTHONPATH=backend python3 -m unittest tests.test_compaction_progress` — 4 tests, green: sidecall forwards deltas, content-only counting with clamped estimate, `expected_chars` as denominator, and full `Agent.compact` emitting `compaction_progress` between `compaction_started` and `compacted` with growing chars, then learning `summary_chars`.
- Neighbours re-run green: `tests.test_compact_over_tokens`, `tests.test_agent_context`, `tests.test_provider`, `tests.test_tool_output_bounds`, `tests.test_sessions`, `tests.test_requests`, `tests.test_loopdetect`.
- `scripts/relay-build --target relay` built; the landing's build gate (`land.py commit`) rebuilt the exact tree green.
- `docs/qa_evidence/2026-09-24-compaction-progress/`: driver run, OCR of the chip in NOTES.md (20% → 38% → cleared).

## Try it
docs/qa_evidence/2026-09-24-tryit-31BM/stage.sh

It opens Relay on a throwaway profile with a long conversation ready on a fake `local:big`, then type `/model local:small` and watch the chip at the bottom of the pane (the one that normally reads “N% left | big”) while the pane says “Compacting the conversation…” — it takes about 24 seconds.

Did the chip tell you how far along the compaction was while you waited — was the climbing percent legible enough that you’d want it there every time? (about 1 minute)

Expected: docs/qa_evidence/2026-09-24-tryit-31BM/expected.md (sealed until you answer)
