---
id: SWPH
type: work
status: executing
labels: [feature, remote, switchboard]
component: [gui, remote]
milestone: beta
workstream: remote
assignee: claude-code
rank: 6a
created: '2026-09-20'
source: 'owner, 2026-09-20, Claude Code session (after #PH0N and #FR1C)'
acceptance: from a paired phone or iPad, by touch, the owner opens the Switchboard from the inbox, sees the cards by stage with the ones waiting on him first, opens a card to read its body and thread, answers a question or comments, moves a card, files a new card by typing or dictating, and starts Discuss, Plan, Execute or Verify on a card; a card that starts waiting on him produces a notification; a guest never sees any of it
links: {plans: [], commits: [38659350, 2c466481, 5eb5699e, 0abb4df0, fd9d2caf, 39c94b91], evidence: [docs/qa_evidence/2026-09-21-swph-board-bridge/, docs/qa_evidence/2026-09-21-swph-board-view/], related: [PH0N, FR1C, 0VT4, W5N2], github: null}
---
# The Switchboard on the phone: cards, threads and the card actions, by touch

## Issue

is it easy for touch to get to and operate the switchboard

yes, the switchboard is the best thing we have to let a phone drive the system. so build that now and open relay when its ready and help me share with my phone / ipad

## Planning notes

Measured on 2026-09-20: the Switchboard cannot be reached from a phone at all. Remote control
publishes panes that have a terminal screen (`RemoteShare::sharePane` refuses a pane with no
`TerminalView`), the Switchboard is a tool pane with none, and `app/` has no board view. The wire's
`FORWARDED_EVENTS` lists the board's events on purpose ("a phone watching a pane should see the
board move"), but those are a *pane* worker's events; the Switchboard itself runs on a separate
per-window `BoardWorker` (`src/BoardWorker.h`, sessions protocol 19) that the remote hub never sees,
and no `board_*` request is accepted from a device. The design doc listed "Board on phone" as a
later, optional item; no card existed.

The shape follows the owner's one-model-two-views rule (#0VT4): the desktop bridges its own
`BoardWorker` to the hub, the hub sanitises and allow-lists, and the phone draws what the desktop's
board draws. Nothing about a card is formatted twice, and the phone never touches the repository.

Not taken: reading `issues/` from the phone through a file API (paths on the wire, which the
protocol forbids); a second worker for remote (two writers on one board); publishing the
BoardPane's pixels (the #0VT4 decision).

## Plan

**Goal.** The acceptance line.

**The contract (all three halves build against this).**

- Phone → hub, `full` devices only, never a guest: `board_request {rid, request}` where
  `request.type` is one of `board_open`, `board_refresh`, `board_card_get {id}`,
  `board_search {query}`, `board_comment {id, text, kind}`, `board_move {id, status, reason}`,
  `board_create {tab, title, request, labels}`, `board_ask {id, text, mode: "discuss"|"plan"}`,
  `board_cancel {id}`, and the GUI-level `board_action {id, action: "execute"|"verify"}`. Never
  from a device: `board_delete`, `board_folder*`, `board_cleanup`, `board_claim`, `set_board`,
  `board_init*`, imports, GitHub sync, anything carrying a path.
- Hub → GUI: `{"t":"board_request","rid":n,"device":"<id>","name":"<device name>","request":{…}}`.
  GUI → hub: `{"t":"board_event","rid":n|null,"event":{…}}` for every event its `BoardWorker`
  emits that the allow-list names (`board`, `board_cards`, `board_card`, `board_changed`,
  `board_thread_appended`, `board_written`, `board_activity`, `board_chat_*`, `board_cancelled`,
  `board_busy`, `board_conflict`, `error` for a request) plus `board_action_result`.
- Hub → phone: `board_event {rid, event}` after `remote/board_state.py` has dropped every path
  field (`path`, `root`, `folder`, `file`), capped lengths, and refused unknown event types.
- A new notification kind `card_waiting`: a card whose `waiting_on` becomes `owner`. It joins the
  phone's "When something needs me" switch.

**Steps.**

1. Desktop bridge (`src/BoardRemote.{h,cpp}`, wiring in `src/BoardWorkspace.*`, `src/RemoteShare.*`,
   `src/RelayWindow.h`): the window's `BoardWorker` started on demand for a remote request, requests
   passed through, events fanned back, Execute and Verify run through the same hooks the desktop's
   buttons use, every write attributed in the status line ("Card moved from iPhone").
2. Hub (`remote/board_state.py`, `remote/wire.py`, `remote/host.py`, `remote/gui_host.py`,
   `remote/notify.py`, `docs/REMOTE-PROTOCOL.md` §17): the allow-lists, the sanitiser, fan-out to
   `full` devices only, rate limits, audit lines, the `card_waiting` push.
3. Phone (`app/board.js`, `app/board.css`, `app/index.html`, `app/app.js`): a Switchboard row at the
   top of the inbox with the waiting-on-you count; stages as collapsible groups, waiting-on-you
   first; the card page (body as sanitised Markdown, thread, tasks); reply box with Comment /
   Discuss / Plan; Move sheet; Execute and Verify; New card with the microphone; touch sizes, no
   hover, works in portrait on a phone and in two columns on an iPad.
4. Drive against the hosted rendezvous, deploy, then open Relay on the owner's desktop and pair
   the iPhone and iPad with him.

**Risks.** `src/BoardPane.cpp`, `src/RelayWindow.h` and `src/Pane.h` are held by other sessions:
the bridge lives in new files and touches those in small hunks. The board worker may not be running
when a phone asks (no Switchboard pane open): the bridge starts it as the pane would. A phone is a
second writer on card files while the desktop edits: all writes go through the one worker, which
already serialises them and answers `board_conflict`.

**Verify.** `tests/test_remote_board.py`, `tests/test_remote_wire.py`, `tests/test_remote_push.py`,
`tests/test_board_view.py`, `ctest -R "boardremote|boardworkspace"`, the hosted drive, then the
owner's devices.

## Tasks

- [x] 1 Desktop bridge: BoardWorker ↔ hub, Execute/Verify through the desktop's hooks <!-- t:s1 -->
- [x] 2 Hub: allow-lists, sanitiser, fan-out to `full` devices, `card_waiting` push, protocol §17 <!-- t:s2 -->
- [x] 3 Phone: Switchboard row, stages, card page, reply/move/actions, new card with the microphone <!-- t:s3 -->
- [ ] 4 Drive, deploy, open Relay and pair the owner's iPhone and iPad <!-- t:s4 -->
