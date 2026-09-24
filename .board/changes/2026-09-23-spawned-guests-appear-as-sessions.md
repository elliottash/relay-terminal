---
id: K9QA
type: work
status: needs-verification
labels: [bug, sessions]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: User report in Relay pane, 2026-09-23
links: {plans: [], commits: [fc8a0a4f53b9441d4db066f04b61835d9025abe8], evidence: [docs/qa_evidence/2026-09-23-spawned-guests/], related: [D2PX], github: null}
---
# Spawned guest agents appear as sessions with Subagent threads off

## Issue
sessions should only list agent sessions. those other ones are duplicates from codex and claude?

can you inspect the sessions rows list more thoroughly and help me make it work well. spawned agents should be considered subagents right? because that box was not checked

## Done means
- The combined Sessions list with Subagent threads unchecked omits native Codex and Claude transcripts launched by Relay for spawned agents.
- Independent Claude and Codex conversations remain findable in the combined list; selecting a guest source alone still exposes its native transcript.
- Checking Subagent threads exposes Relay's saved subagent rows without duplicate native guest rows.

## Plan
**Goal:** use transcript provenance to distinguish Relay launched guests from independent conversations in the combined Sessions list.

**Findings:** `src/Conversations.cpp` hides only `source=subagent` rows when the checkbox is off. `backend/relay_core/conv_index.py` folds guest rows linked to a main Relay session but not guest runs launched for subagents. Codex rollouts record `originator=relay`; Relay launched Claude transcripts use the SDK entry point, while the independent QA conversation uses the CLI entry point.

**Steps:**
1. Persist guest provenance during transcript indexing and migrate existing index rows through the next guest reconcile.
2. Exclude Relay launched guest transcript rows from combined-source searches while retaining explicit guest-source searches.
3. Verify the source combinations and the user's `SJTR` search against focused tests and the local index.

**Risks:** historical index rows need a guest reconcile before provenance is populated; source-only views must retain native transcripts.

**Verify:** focused guest parser, index search and migration tests; compare `SJTR` results and standalone Claude visibility before and after reconcile.

## Execution Summary
Indexed whether a native Claude or Codex transcript was launched by Relay, and filtered those rows from the combined Sessions list. Relay's saved subagent threads remain governed by the Subagent threads checkbox. Independent guest conversations remain visible in combined search, and source-only searches still expose native guest transcripts. Added an in-place v7 → v8 migration and forced transcript reparse through the parser version. Evidence: `docs/qa_evidence/2026-09-23-spawned-guests/README.md`. The running GUI has not yet loaded this backend revision; visual verification remains.

## Tests
- `PYTHONPATH=backend:tests python3 -m unittest test_guest_sessions test_conv_index` — 235 passed.
- `python3 -m py_compile backend/relay_core/guest_sessions.py backend/relay_core/conv_index.py tests/test_guest_sessions.py tests/test_conv_index.py` — passed.
- `git diff --check` — passed.
- Isolated temporary index using saved `SJTR` Relay session, four subagent threads, and three native Codex transcripts — combined result one agent; thread-inclusive result one agent and four subagents; Codex-only result three native transcripts. See `docs/qa_evidence/2026-09-23-spawned-guests/README.md`.
- `python3 scripts/relay-board.py check` — repository-wide check reported 14 errors and 758 warnings on unrelated existing cards/threads; none reported for #K9QA.
