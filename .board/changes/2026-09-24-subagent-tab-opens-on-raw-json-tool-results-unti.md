---
id: M5FZ
type: work
status: needs-verification
labels: [bug]
assignee: agent
implemented_by: glm/glm-5.3
session: 58506d2c-6f97-4190-b6b4-7d442f882f71
rank: zzzzzzzzzzzzzzzzzz
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-24-m5fz-subagent-snapshot/], related: [], github: null}
---
# Subagent tab opens on raw JSON tool results until live events arrive

## Issue
when i first click a subagent, its showing weird unformatted json. can you make it start off formatted nicely

## Done means
Opening a subagent tab (or the turn transcript) shows the same one-line folded tool rows the live view shows — no raw `json.dumps` tool results on first paint. Raw JSON stays available one fold-click away. Proved by the backend subagents test (snapshot carries `tool` + § 23 `label` per tool message) and the C++ subagents test (snapshot renders the label line, not the JSON).

## Tests
- `PYTHONPATH=backend python3 -m unittest tests.test_subagents` — 41 OK, includes new `TranscriptItemsTests` (landed calls carry `tool` + § 23 `label`; running call keeps its ⚙ name; unpaired tool message stays raw).
- `PYTHONPATH=backend python3 -m unittest tests.test_routing_thinking_skills` — 29 OK, `turn_transcript` assertions updated to the paired shape.
- `./build/relay-subagents-tests` — 33 passed, includes new `transcriptSnapshotOpensOnToolRows` (snapshot opens on `▸ read …` row, raw JSON not in first paint, fold holds it, ⚙ keeps the pending call).
- Evidence: `docs/qa_evidence/2026-09-24-m5fz-subagent-snapshot/tests.txt`. (`trackerOmitsRoleTagsIncludingHistoricalRows` flaked once in a full-suite run, passes alone and on rerun — pre-existing, separate card.)

## Execution Summary
- `backend/relay_core/agent.py` — new `transcript_items(messages)` pairs each landed tool call (by `tool_call_id`) with its message: the tool item gains `tool` (name) and `label` (`tool_labels.result_label(name, args, result)`, args re-parsed from the recorded call), and the assistant item's `tool_calls` keeps only calls whose result has not landed, with the `(pending)` suffix preserved. `turn_transcript` uses it.
- `backend/relay_core/subagents.py` — `subscribe` sends `transcript_items(...)` instead of its inline bare-content comprehension, so the `subagent_transcript` snapshot carries the pairs.
- `src/SubagentTranscript.cpp` — a paired snapshot tool item renders as the same folded row the live view shows (marker, label line, `✗` on failure); the fold holds the raw result (capped 8000). Unpaired items (older worker) keep the capped-lines fallback.
- `src/TurnTranscript.cpp` — same: label line for paired tool items, capped lines otherwise.
- Landed as `eab7300a` (build-gated by land.py on the exact tree) and `bdc20cfe` (routing-test follow-up).

## Try it
One task, one question, about two minutes. No model, key or network involved.

1. Run `docs/qa_evidence/2026-09-24-tryit-M5FZ/stage.sh` (it prints two `staged:` lines when ready; safe to run twice — it resets).
2. In the Relay it opened on display :96, click the **notes QA sweep** row ("done · 0:00 · 2 tools") in the subagents strip, and scroll the tab's transcript strip up with the wheel to its first paint.
3. Question: does the transcript open on one concise line per tool call (`read notes.md`, the `grep` row) with no raw `json.dumps` JSON dump anywhere — the fold still opening the raw result when clicked?

The scripted situation is already verified mechanically (`ai-pass.sh` in the same folder exits 0: the tab opens, counts the two tools, no raw JSON on screen); what the pass cannot read is how it looks. A pre-change Relay shows the raw JSON dumps instead — that is the bug.
