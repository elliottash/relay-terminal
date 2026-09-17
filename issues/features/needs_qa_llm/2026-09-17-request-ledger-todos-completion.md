---
id: 9PGA
type: work
status: needs-qa-llm
component: [agent, worker]
milestone: desktop-alpha
workstream: agent
assignee: implemented by Claude Opus 5 (Claude Code, backend), 2026-09-17. Not committed.
rank: sg
created: '2026-09-17'
acceptance: '`tests/test_requests.py` (39 tests; full suite 272 tests OK), updated `tests/test_agent.py` and `tests/test_queue.py`; live scenarios in `docs/qa_evidence/2026-09-17-request-ledger/` (see `NOTES.md`)'
source: 'owner request 2026-09-17 ("make sure things dont get missed when i type multiple requests, and across long convos"); research and owner decisions in `docs/MEMORY-AND-MULTI-REQUEST-RESEARCH.md` sections 6, 7 and 9 (items 1, 2, 3, 4, 5, 7, 8; backend only). Protocol: `docs/AGENT-SESSIONS-PROTOCOL.md` section 12.'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Backend: request ledger, todos, completion check, drop-path fixes, compaction that keeps asks

## Behavior

- **Turn limits (G1):** default 50 model steps and 150 tool calls (`max_steps`, `max_tool_calls` in `configure` and
  `set_agent_options`). At the limit the turn ends with `done {stop_reason: "limit", limit, open_items}`; the queue
  is not paused and the request stays open.
- **Cancel, interrupt, failure (G2):** the prompt, delivered steers and subagent notes stay in the conversation; a
  half-finished tool-call group gets "not completed" results; a note says the request is unfinished.
- **Steers (G3):** framed with ledger id and time, keep attachments and program context.
- **Turn starts (G5):** Relay tags its user messages (`relay_kind`, `relay_requests`); only prompts are turn starts.
  The provider strips `relay_*` keys before sending.
- **Summarizer input (G7):** user messages are never middle-trimmed; older assistant/tool messages are dropped first.
- **Request ledger** (`backend/relay_core/requests.py`): `R<n>` per prompt at submission, statuses, `requests` event,
  `requests` / `request_get` / `request_set` / `request_reask` commands, saved in the session, consistent across
  resume, fork (up to the turn) and rewind; legacy sessions are backfilled from checkpoint prompts.
- **Todos** (`backend/relay_core/todos.py`): `update_todos` tool (whole-list replace, one in progress, reasons for
  cancelled/deferred/blocked, links to requests), `todos` event and command, stale reminder after 8 steps.
- **Compaction:** new summary sections, merge rule, carried block (requests verbatim, todos, plan, files, subagents,
  recent user messages up to min(20K tokens, window/16)), handled steers marked; `compacted.carried` stats.
- **Completion check:** at most 2 automatic re-prompts per turn (`completion_check` event), then
  `done {open_items}`. Toggle `completion_check`.
- **Audit:** `audit_requests` (off by default) runs a flag-only no-tools call on the route-assist model (falls back
  to the pane's model at low effort) → `request_audit`, flags stored on ledger entries.

## QA checklist

1. `./scripts/test.sh` passes; `cmake --build build` builds; the existing GUI still runs a queued conversation
   (additive events only: `requests`, `todos`, `completion_check`, `request_audit`; extra fields elsewhere).
2. Read `docs/AGENT-SESSIONS-PROTOCOL.md` section 12 against the code: every event, field and command named there
   exists with that shape (`backend/relay_core/{agent,queue,requests,todos,session_protocol}.py`, `backend/worker.py`).
3. Drop paths: confirm a regression test fails on the old behavior for each of G1, G2, G3, G5, G7
   (`tests/test_requests.py`: `test_g1_*`, `test_g2_*`, `test_g3_*`, `test_g5_*`, `TranscriptTests.test_g7_*`).
4. With a stored key, run `scripts/eval-requests.py --preset <preset> --scenarios 1,2,3,4` (and `6` for compaction)
   and check files on disk and ledger statuses in the summary JSON; try `--no-todos` for comparison.
5. Worker by hand: `set_agent_options {max_steps: 2}` then an ask that needs several tools → `done` with
   `stop_reason: "limit"`, `agent_finished` not followed by a paused queue.
6. Resume a session saved before this change: `requests` lists its prompts as `done`; `sessions` items have
   `open_requests`.
7. Confirm no test touches the keyring or network (router provider and `keystore.lookup` are patched in the audit
   tests) and that no key appears in the evidence files.

## Known gaps

- GUI work (Requests chip, open items on recap, Continue on limit, Agent options entries for the new settings) is not
  done; the GUI shows the limit only through the `status` text.
- Keeping a failed turn in history means a failure caused by the conversation content itself can repeat on the next
  turn (the old rollback avoided that); rewind or a new chat recovers.
- Turn-opening prompts are not labelled with their `R<n>` in model-visible text; new todos link to the turn's opening
  request by default.
- The eval runs per preset × 3 runs × with/without todos (research section 7) were not done; only single live runs.
