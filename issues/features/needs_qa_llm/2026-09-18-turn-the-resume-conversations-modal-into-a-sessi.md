---
id: R6J0
type: work
status: needs-qa-llm
labels: [feature]
component: [gui, worker]
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (Claude Code subagent, session-manager work), 2026-09-18
rank: zzzzy
created: '2026-09-18'
source: issues/feature_intake.txt, 2026-09-18
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-session-info-and-manager/'], related: [Y63Z, CCKY, AE08, K7RY], github: null}
---
# Turn the resume-conversations modal into a session manager pane

## Issue
make the resume convos modal into a session manager pane. make "subagent threads" findable there, but they are unchecked by default. and they should indicate the owner session.

## Decisions (owner, 2026-09-18)
- Step two of the work #Y63Z starts: the session manager is a **pane**, not a modal, and it is the
  one surface for `/resume`, Ctrl+Shift+Y (`agent.resume`, #K7RY), `conversations.open` and the
  palette's Resume row. Ctrl+Shift+O stays Options.
- "Subagent threads" is a checkbox, **unchecked by default**; when ticked, threads are listed and
  searchable, each under (or naming) its owner session.
- Other features may add tabs to the pane (recently closed windows/tabs/panes, session d4).

## Change (2026-09-18)

The resume picker (a four-column modal over one workspace, capped at 200, no search) and the
conversation dialog (#CCKY) are now **one pane**, `relay::conversations::SessionManager`
(`src/Conversations.{h,cpp}`), opened beside the pane that asked (`paneType` `sessions`), one per
tab, bound to that pane's worker.

- Every session, newest first, grouped by project; This project / All projects; Everything /
  Agent sessions / Terminal history; model, date, open tasks; sort (newest, oldest, most turns,
  most matches); "Show more" pages past 100; full-text search (words now AND over the whole
  conversation, not one message); preview with the matches highlighted; Info, Rename, Pin, Delete.
- **Subagent threads**, unticked at first. Ticked, each thread hangs under its owner session, a
  nested thread under its parent; when the owner is not among the results it still heads its
  threads as a muted row (Enter on it resumes it). Threads are found by search (their tasks,
  tool calls, outputs and reports are indexed). Preview header: "Subagent thread of “owner”".
- Enter resumes the session in the pane that opened the manager, Shift+Enter in a new pane (as
  the modal did); if some pane already has that session open, that pane is focused instead, so
  two workers never autosave one file. Enter on a thread opens its history in the ⓘ pane (#Y63Z).
  Ctrl+I (or Info) opens a session's ⓘ view without resuming it. Esc closes and returns.
- `/resume words` opens it searching for them. Hints: `/resume` and `/conversations` → Ctrl+Shift+Y.
- `reset` now carries the new session id, so after `/new` the pane no longer takes the old session
  for its own.
- Tabs for other features: `RelayWindow::addSessionsTab(id, label, factory)` and
  `RelayWindow::openSessions(tab, query)`; `SessionManager::addTab/showTab`.

Backend (section 14 of the protocol, extended): `conversations` takes `include_threads`, `sort`,
`offset`, `matches_per_item` and returns `next_offset`; thread rows carry `owner_session`,
`owner_title`, `parent_thread`, `parent_title`, `agent_id`, `agent_type`, `spawn_turn`, `status`;
renames and pins go to the session files; deleting a session deletes its threads.

Tests: `tests/conversations_test.cpp` (threads off by default, `include_threads` sent when ticked,
threads under their owner and a nested one under its parent even when it arrives first, a muted
owner row, Enter on a thread opens its history and never resumes, extra tabs, Esc) and
`tests/test_session_threads.py`, `tests/test_conv_index.py` (backend).

Evidence (`docs/qa_evidence/2026-09-18-session-info-and-manager/`): 05 the manager, threads off; 06 ticked, a1 and a2 under their owner;
07 All projects, the other project's session; 08 a search that finds only threads, under a muted
owner row; 09 Enter on the nested one opens its history; 10 `/resume capybara` prefills the
search; 11 Enter resumed the session in the pane ("Session loaded … 2 turn(s)"); 12 the same
session asked for from a second pane focuses the first.

## QA checklist
1. Ctrl+Shift+Y, `/resume`, `/conversations`, the palette's Resume session… and Conversations…, and
   the title bar's Sessions button all open the same Sessions pane beside the pane; Esc closes it
   and puts the focus back.
2. The list shows every session of this project, newest first; switch to All projects and other
   projects appear under their own group.
3. "Subagent threads" is unticked. Tick it: threads appear under the session that started them,
   labelled `↳ a1 general · description`, with status and model; a search for a word only a
   subagent saw finds the thread, under its owner.
4. Enter on a session row resumes it here ("Session loaded: …"); Shift+Enter opens a new pane with
   it. Ask for the same session from another pane: the pane that has it is focused, nothing loads
   twice.
5. Enter on a thread row opens its history in the ⓘ pane, with "↑ owner session".
6. `/resume word` opens the pane with the search filled in.
7. Rename and pin a session, restart Relay: the name and the pin are still there
   (`<id>.meta.json` holds them). Delete a session with threads: its `.threads/` folder is gone.
8. An install with sessions saved before the index existed: the first open of the pane lists
   them all (the reconcile), without "Rebuild the conversation index".
