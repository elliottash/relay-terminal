---
id: 6P8N
type: work
status: done
labels: [bug, guest, prompts]
assignee: codex
rank: m
created: '2026-09-23'
source: 'Codex in a Relay pane, 2026-09-23'
links: {plans: [], commits: [09ae54d3577aa85b9ef62719d5d183e3e81aee3f], evidence: [tests/test_guest_delegation.py], related: [GPF7], github: null}
---
# Remove repeated guest prompt preamble and audit prompt overhead

## Issue
ok, remove that and then scan for any other extraneous / inefficient prompting

## Done means
Guest turns no longer prepend the relay_board instruction as user text; a fresh guest prompt without dynamic turn context begins with the actual request, so its fallback title does not start with the board paragraph.
The audit identifies other recurring prompt additions and removes any clearly redundant text without dropping needed per-turn state.
Focused guest prompt tests pass. Failure is a sent guest prompt still starting with the board preamble or carrying an empty task-state block.

## Plan
**Goal:** Stop the repeated prompt text that pollutes guest transcripts and titles, and trim other clear guest prompt overhead.

**Findings:** `backend/relay_core/guest_harness_provider.py` prepends the board paragraph in `complete()` on every turn, while `backend/relay_core/guest_instructions.py` already supplies board guidance through native guest instructions. The same `complete()` appends a task snapshot even when it is empty. Guest title fallback reads the transcript's first user prompt in `backend/relay_core/guest_sessions.py`.

**Steps:**
1. Remove the board paragraph from `HarnessProvider.complete()` and omit the empty task snapshot.
2. Audit other prompt additions for repeated static text, preserving dynamic context and one-time handover briefs.
3. Add focused fake-harness regression coverage and run relevant guest tests.

**Risks:** The board bridge must still begin and end around every turn; guests still need their native instruction supplement and current nonempty task state. Do not change the user's project instructions or another session's files.

**Verify:** Run `tests.test_guest_harness_provider` and nearby guest prompt tests; inspect the exact sent prompts and bridge discovery test.

## Execution Summary
Removed the static relay_board paragraph from every guest turn. `guest_instructions.py` still supplies tool-discovery and board fallback guidance through Claude's system-prompt supplement and Codex's developer instructions. The per-turn bridge lifecycle remains in place.

An empty task list no longer adds `[Relay tasks: current state] []`; nonempty task state still travels with the turn. The prompt audit found the other additions serve distinct purposes: a one-time handover after a model switch, and per-turn terminal/plan state that can change. Terminal context can still precede the request in a guest transcript, so guest title fallback may show `[Relay context...]`; that is a separate title presentation issue, not the redundant board paragraph removed here.

## Tests
`PYTHONPATH=backend:. python3 -m unittest tests.test_guest_harness_provider tests.test_guest_delegation tests.test_guest_handover tests.test_plan_turns tests.test_guest_sessions -q` — 232 passed.
`git diff --check` — passed.
