---
id: H6VQ
type: work
status: needs-verification
labels: [bug, agent-app-control, sessions, switchboard]
assignee: claude-code
rank: m
created: '2026-09-20'
source: 'owner report, 2026-09-20 23:22, relayed to a Claude Code session'
links: {plans: [], commits: [fd4cb3c1, 75af8a42, 3eebe40a], evidence: ['docs/qa_evidence/2026-09-20-helper-opens-sessions/'], related: [FEJQ], github: null}
---
# The Sessions helper could not open a conversation, and nothing answered its app_open

## Issue
sessions helper didn't do anything when I asked to open a group of previous sessions in new panes.

Later, after the first fix:

cool, it partly worked. can you make that more formalized that it can do that? but it also needs
to reply in text that it is doing it.

message queue isn't working in the sessions helper, i can't interrupt.

## Decisions
- "can you make that more formalized that it can do that?" — opening a conversation is a
  documented ability of the Sessions helper: its own paragraph in the pane's brief, in the
  `agent.app` prompt section and in protocol §30.4, not a line in a tool schema.
- "it also needs to reply in text that it is doing it" — the helper states what it is doing in
  one line before or alongside any app action, and a turn that acts and answers with nothing has
  a summary appended from the tool results, because the panel draws text and never tool calls.
- "in new panes" means `new_pane: true`; that is also the default, since loading a conversation
  into the pane the person is sitting in takes that pane's own conversation away.

## Execution Summary

Two faults, one on each side of the pipe, and two things the owner asked for on top.

1. **The GUI never answered a helper's `app_command`** (`fd4cb3c1`). `RelayWindow::boardWorker`'s
   `onEvent` read the event's name out of `type`; a worker names its events in `event` — `type` is
   what the GUI calls the messages it sends *down*. It had said `type` since the helper route was
   written (`c78c8004`, the same day), so the branch matched nothing: every `app_open`,
   `app_option_set`, `app_action_run` and `app_undo` a tab's helper sent went unanswered, and the
   `presets`/`key_tested`/`configured` handling below it was dropped with them. In the owner's run
   the call was still hanging when the tab it was asked in closed, which stops a tab's helper
   (§30.7) and killed it — `app_open … ok=False ms=5358` in the same millisecond as `worker_stop`.
2. **`app_open` gained `target: "conversation"`** (`fd4cb3c1`): `id` for one, `ids` for up to 8,
   each in a pane of its own, in the order given, one round trip each so the result answers per id
   and one unknown id does not lose the rest. The worker resolves every id against the conversation
   index it already owns and sends the whole row — the GUI holds no index, and a claude or codex
   row reopens by its own argv (26.7). The window routes it to `Pane::openSavedSession`, which is
   the Sessions row's Enter. `new_pane` defaults to true. Unknown ids answer `unknown_conversation`.
   `app_sessions_search` now gives each row the `id` that open takes: it asked the index for "id"
   and the index calls it `session_id`, so no row had ever carried one.
3. **Formalized, and narrated** (`fd4cb3c1`). Opening a conversation is a paragraph of the Sessions
   helper's brief, of the `agent.app` prompt section and of protocol §30.4 — not a line in a tool
   schema — and all three carry the rule that the agent says in one line what it is doing before or
   alongside any app action. The panel draws text and never tool calls, so the worker enforces the
   floor too: a page-agent turn that ends with no text of its own after an `app_*` call has that
   call's own sentence appended, which is what the panel shows.
4. **A worker that dies mid-turn puts its panels back** (`75af8a42`). `BoardWorker` reports an exit
   nobody asked for (`onExit`); the window turns it into the turn's own `error`, tagged with the
   pane that asked and delivered to that tab's board panes *and* its embedded Options, Actions and
   Sessions panels, so the busy strip goes, the composer takes prompts again and the line lands in
   the log. The next ask starts a fresh worker through the first-ask path. `board_chat_cancelled`
   now carries the `pane` that pressed Stop, so the panel that asked settles instead of waiting for
   an answer addressed to somebody else.
5. **The queue is drawn in every pane** (`3eebe40a`). It always was the worker's and always served
   every panel, but only the Switchboard built the rows; the other three promised in the composer's
   placeholder that a second prompt queues and then showed nothing. A dead worker's queue is
   dropped with the turn.

## Tests

- `python3 -m unittest tests.test_app_tools` (58 pass) — the new target, the `ids` form, per-id
  results, the guest row's resume command, every refusal, and the search row's `id`
- `python3 -m unittest tests.test_board_chat` (94 pass) — the Sessions brief, the "say what you are
  doing" rule in every pane, the narration fallback and the four ways it must not fire, and a
  cancel from the Sessions panel answered to the Sessions panel
- `ctest --test-dir build -R appcommands` — `conversationToOpen`, and `open conversation` through
  the executor with writes off
- `ctest --test-dir build -R boardworkspace` — the helper still outlives its panels and only
  `forgetTab` releases it; the death path exists end to end
- `ctest --test-dir build -R board` (`tests/boardmodel_test.cpp`) — a Sessions panel draws a queued
  prompt, removes it, stops its own turn and settles on the answer addressed to it; a worker that
  died leaves it idle with the queue gone and the composer usable
- `manual: docs/qa_evidence/2026-09-20-helper-opens-sessions/` — 12 PASS, 0 FAIL, plus the
  before-fix reproduction through the real worker

## QA checklist

- [ ] In a Sessions pane, ask its helper to open several past conversations "in new panes": each
      one opens in a pane of its own, in the order it named them, and the panel says so in words.
- [ ] Ask it to open one "here": it loads into the pane you are in and says which.
- [ ] Ask for a conversation that does not exist: one sentence naming it, and the others still open.
- [ ] While the helper is answering, type a second prompt in the Sessions (or Options, or Actions)
      panel: a "1 prompt waiting" row appears with a × that takes it out again.
- [ ] Press ✕ Stop in that panel: the turn is marked cancelled there and the queued prompt starts.
- [ ] Close the tab a helper is answering in, then ask again in another tab: nothing hangs.
- [ ] With Options › Agent's "Agents may change options and run actions" **off**, opening a
      conversation still works (opening is not a write, §30.4).
