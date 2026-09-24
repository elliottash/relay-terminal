---
id: Y63Z
type: work
status: needs-qa-llm
labels: [feature]
component: [gui, worker]
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (Claude Code subagent, session-manager work), 2026-09-18
rank: zzzzw
created: '2026-09-18'
source: issues/feature_intake.txt, 2026-09-18
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-session-info-and-manager/'], related: [R6J0, CCKY, WD83], github: null}
---
# A conversation info button on the pane, with a traceable history including subagent threads

## Issue
add a conversational info button at the top right of the pane (i in a circle). it then shows convo info sort of like in warp, or the /status command in claude / codex. and it will have a traceable conversation history, including a list of subagent threads with links to them, which would have their own history. that gives a way to track convo history including the subagents previously used with associated threads.

## Decisions (owner, 2026-09-18)
- One piece of work in two steps with #R6J0: first a session/thread index recording every session
  and every subagent thread with its owner session, then the ⓘ view on it; then the session
  manager pane (#R6J0).
- The info view is a **pane** (owner prefers panes to overlays), docked beside the pane it
  describes; Esc returns. `/status` opens the same view, and a click on ⓘ hints at `/status`.
- Subagent links in the history open that thread's own history in the same view, with a way back
  up to the owner session; a thread still held by the pane's worker also opens in the tabbed
  subagents pane (`RelayWindow::openSubagentTab`, #WD83).

## Change (2026-09-18)

**Threads are saved.** Every subagent is now a thread with a durable id, written beside the
session that started it — its owner session — when it starts and whenever a run ends:
`<session_dir>/<owner>.threads/<thread-id>.json` (0600), with `owner_session`, `parent_thread`,
`spawn_turn`, `spawn_call`, status, model(s), provider-reported usage and the messages
(`backend/relay_core/subagents.py`, `sessions.py`). Subagents cannot start subagents today, so
`parent_thread` is empty for everything Relay writes now; the index, the view and the session
manager place a thread under its parent when it is set (tested with seeded files).

**The session file records usage and models**: `usage` (the provider's own prompt/completion/total
tokens and request count, and `cost` once a provider reports one — OpenRouter does; otherwise the
view says "not reported by this provider"), `models` (every model the session ran on) and the
instruction files loaded (`agent.py`).

**The index** (`conv_index.py`, schema v2) holds thread rows (`source: "subagent"`) beside the
sessions, linked by `owner_session`/`parent_thread`. A v1 index is migrated in place, not wiped
(terminal history has no file to rebuild from). Once per worker it reconciles itself with the files
(sessions saved before the index existed, or with it off, were never found before). User titles and
pins moved from the index into `<id>.meta.json`. One function spells a workspace path for the
session directory and the index, so a symlinked workspace finds its own sessions.

**`session_info`** (protocol section 25) returns the live session (model and provider, context
used/window, tokens and cost, session id and file, started/updated, turns, instructions, git
branch) and its history — turns in order, each thread at the turn that started it — or one thread
and its own history (messages with tool calls, the threads it started placed after the call that
started them, "owner session" and "parent thread").

**GUI.** A painted ⓘ (circle and i drawn with QPainter, not a font glyph) is first in every agent
pane's header row. It, `/status` and `/info` open the info pane beside the pane
(`src/SessionInfo.{h,cpp}`); thread links open the thread's history with "↑ owner session" (and
"↑ parent thread"), Alt+Left goes back, F5 refreshes, the file links open the JSON in a preview,
and the view refreshes itself when a turn ends. `agent.info` is registered (unbound, so the Actions
pane lists it), and a click on ⓘ shows "Next time: /status in the prompt box".

Tests: `tests/test_session_threads.py` (15: usage totals, the session file, thread files beside the
owner and deleted with it, owner/parent links in the index, threads off by default and findable
when asked, reconcile and rebuild, titles and pins in the meta file, the v1 migration keeping
terminal history, the symlinked workspace, sort and paging, a real subagent run saving its thread
with owner/turn/call/status, `session_info` live and saved, nesting); `tests/conversations_test.cpp`
(render of the session and thread pages, link encoding, navigation, back, stale answers, Esc).

Evidence (`docs/qa_evidence/2026-09-18-session-info-and-manager/`, `drive.sh` + `fake-provider.py`, offline): 01 the ⓘ in the header,
02 the info pane with both subagent links at turn 1, 03 a thread's history with "↑ owner session",
04 back on the owner, 09 a nested (seeded) thread with "↑ owner session" and "↑ parent thread".

## QA checklist
1. In an agent pane with a configured model, click ⓘ (first button, top right of the header). A pane
   opens beside it: model and provider, context, tokens (and cost, or "not reported by this
   provider"), the session id and its file (a link that opens the JSON), started/updated, turns,
   instructions. The "Next time: /status" hint shows (once per the hint limits).
2. `/status` opens the same pane; `/info` too. Esc closes it and the focus is back in the pane.
3. Ask for work that starts subagents ("use two subagents to …"). Reopen ⓘ: turn N lists each
   thread (agent id, type, description, status, model) at that turn. After another turn ends while
   the pane is open, it refreshes itself.
4. Click a thread (or Tab to it and Enter): its history — task, tool calls, output, report — with
   "↑ owner session" first. Follow it back; Alt+Left also goes back. "open in the subagents pane"
   opens the thread's tab in the subagents pane.
5. `ls ~/.local/share/relay/sessions/<digest>/<session>.threads/` holds one 0600 JSON per thread
   with `owner_session`, `spawn_turn` and `status`. Delete the session from the session manager:
   the folder goes with it.
6. With an OpenRouter model, Cost shows a dollar amount; with GLM/Kimi it says not reported.
7. Close and restart Relay, resume the session: ⓘ still lists the threads (read from the files)
   and their histories open.
