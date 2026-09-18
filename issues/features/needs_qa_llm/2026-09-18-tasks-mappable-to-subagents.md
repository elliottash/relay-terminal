---
id: QHR1
type: work
status: needs-qa-llm
labels: [feature]
component: [agent, worker, gui]
milestone: desktop-alpha
workstream: agent (subagents, tasks)
assignee: agent
implemented_by: Claude Opus 5 (Claude Code, subagent of the main session, in the shared checkout), 2026-09-18
rank: zzzzzz
created: '2026-09-18'
source: issues/feature_intake.txt, 2026-09-18
acceptance: '`tests/test_todo_subagents.py`, `tests/requests_test.cpp` (ctest `requests`), `tests/subagents_test.cpp` (ctest `subagents`), live run in `docs/qa_evidence/2026-09-18-tasks-mappable-to-subagents/`'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-18-tasks-mappable-to-subagents/], related: [WD83, M9T4], github: null}
---
# Tasks mappable to subagents

## Issue
the tasks list should be mappable to subagents.

## Decisions

Implementer's reading (Claude Opus 5, 2026-09-18), to be confirmed or overruled by the owner:

- **"Mappable" means a task can be given to a subagent, and the mapping is visible and live both
  ways.** The task row says which subagent has it; the subagent's strip row, tab and ✦ lines say
  which task it works on; and the task's status follows the subagent while it runs.
- **Both the model and the user can map a task.** The model starts a subagent with
  `agent(todo_id="T3")`. The user selects a task in the task list and presses **S** (or right-click →
  "Run as subagent"); Relay starts a background `general` subagent on it. One subagent per task at a
  time; a task keeps the id of the last subagent that had it as a record.
- **Status follows the subagent** (as requests already follow todos): in_progress while it runs;
  done → completed; failed → blocked with "Subagent a2 failed: <error>"; stopped → pending again
  with a note (a stop is not a failure, and nobody did the work). A subagent resumed with a message
  takes its task back to in_progress unless another subagent has it now.
- **The one-in_progress rule relaxes for tasks a subagent is running**: they do not count, so the
  main agent can work one task while subagents work others in parallel. While a subagent runs a
  task, the model's `update_todos` cannot change that task's status (the subagent owns it); after it
  ends the model can (e.g. to reopen a task whose report shows it was not really done — the result
  note tells it so).
- **The main turn may end while a subagent runs a task**: the completion check and the stale-list
  reminder skip delegated tasks, and the GUI counts such a task as active, not unfinished. The
  request stays open until the task settles.
- **What the user can hand over:** any task that is not completed or cancelled — pending, in
  progress, deferred, or blocked (so a failed subagent's task can be retried). Not one a subagent
  is already running.
- **What a user-started subagent knows:** it cannot see the conversation, so its task is the todo
  text, any note on it, and the verbatim text of each request it serves. The main agent is told "The
  user handed todo T3 to background subagent a2 … do not work on it yourself" before its next model
  call (a note, never a wake-up by itself); the subagent's result reaches it like any background
  result, with a line saying what Relay did to the task.
- **After a restart** no subagent of the saved session runs, so a task one was still running loads
  as pending with a note, its link kept. A rewind keeps the links of subagents still running.

## Change

- Backend: `backend/relay_core/todos.py` (the `subagent` field, `TodoList.delegated`, the list's
  lock, `check_delegable`, `subagent_started`/`subagent_finished`, the relaxed rule in `validate`,
  load and rewind), `backend/relay_core/agent.py` (`todo_for_subagent`, `todo_subagent_task`,
  `todo_subagent_event`; completion check and stale reminder skip delegated todos),
  `backend/relay_core/subagents.py` (`todo_id` on `agent`, `Subagent.todo_id`, the hooks at start,
  resume and finish, `notify_main`, the todo line in results), `backend/worker.py` (`todo_subagent`).
- GUI: `src/RequestLedger.*` (`LedgerTodo::subagent`, `subagentRunning`, `delegable()`; a running
  delegated todo is Active), `src/RequestsPanel.*` (the `✦ a2` on rows, the detail line, Enter /
  double-click, S, the row menu), `src/SubagentsPanel.*` (`SubagentRow::todoId`, "T3 · …"
  description), `src/Pane.h` (wiring, the `todo_subagent` reply, two hints).
- Protocol: `docs/AGENT-SESSIONS-PROTOCOL.md` sections 8 and 12.4. Architecture: "Tasks UI" in
  `docs/ARCHITECTURE.md`.
- Hints: a mouse open of a task's subagent → "Next time: Enter · on a task opens its subagent"; the
  menu's "Run as subagent" → "Next time: S · on a task runs it as a subagent".

## Evidence

`docs/qa_evidence/2026-09-18-tasks-mappable-to-subagents/` — `drive.sh` runs a fresh, isolated
Relay under Xvfb against `fake-provider.py` and takes seven screenshots (see its header);
`requests.jsonl` is what the fake model received. Tests: `tests/test_todo_subagents.py` (10),
`requests_test.cpp::panelMapsTasksToSubagents`, `subagents_test.cpp::todoSubagentNamesItsTask`.

Note for QA: on 2026-09-18 the live run used `RELAY_DATA_DIR` pointing at a copy of `backend/` whose
keybinding id pattern accepted `_`, because another session's uncommitted `ssh.split_same_host`
action in `src/Keymap.h` made every `configure` fail with "Invalid action id". Once that is settled,
run `drive.sh` without it.

## QA checklist

- [ ] `./scripts/test.sh` passes `test_todo_subagents`; `ctest --test-dir build -R 'requests|subagents'` passes.
- [ ] A model that calls `agent` with `todo_id` gets `todo_id` back; the todo shows `subagent`, is
      in_progress, and goes to completed / blocked (with the error) / pending (with a note) when the
      subagent finishes / fails / is stopped.
- [ ] With one todo delegated, `update_todos` may put another in_progress; resending the delegated
      one with another status does not change it.
- [ ] A turn whose only open todo is delegated ends without a completion-check nag; the Tasks chip
      and panel count that todo as active, not unfinished; the request becomes done when the
      subagent completes it.
- [ ] Task list: a delegated row shows `✦ a<n>`; Enter and double-click open that subagent's tab; S
      and the row menu hand a pending, deferred or blocked task to a new subagent and refuse a
      completed, cancelled or already running one.
- [ ] The subagent's strip row, tab header and ✦ lines read "T<n> · …".
- [ ] The main agent's next model call after S carries the "user handed todo … to background
      subagent …" note; the subagent's task carries the todo text and the request verbatim.
- [ ] Saving with a delegated todo and loading it again: pending, note "not running any more", link kept.
- [ ] The two mouse paths show their hints (at most 3 times each, global hint setting respected).
