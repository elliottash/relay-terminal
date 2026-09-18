---
id: C4M8
type: work
status: needs-qa-llm
labels: [change, feature]
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (Claude Code subagent), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: 'The queue strip is one list in delivery order: steers waiting for the next tool call are selectable rows at the top, reached with ↑, withdrawn with Shift+Delete or ×, taken back into the prompt box by editing, moved back to the queue with Ctrl+↓; Ctrl+↑ on the head queued prompt makes it a steer; queued rows edit, reorder and remove as before; `Pane::removeRow` and `Pane::queueRows` exist; queuenav tests pass'
source: 'owner, bug intake 2026-09-18, and the approved one-list design'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-one-queue-list/'], related: [Y4GE], github: null}
---
# The queue strip is one list: steers are rows like the rest

## Issue

you should still be able to cancel a task that is queued for the next tool call, if its still waiting.

(no shortcuts, give me what we will need for the final app)

## Change

#Y4GE gave the "↪ next tool call" line a × of its own, but steers were still labels outside the
list: no selection, no keys, no editing. Now every row the pane will deliver is a row of one
`QListWidget`, in delivery order, drawn by `QueueRowDelegate` from a kind role:

| Row | Delivered | × / Shift+Delete | ↑ then Enter or typing | Ctrl+↑ / Ctrl+↓ |
|---|---|---|---|---|
| ▸ running (a label, not a row) | now | — (Esc stops the turn) | — | — |
| ↪ next tool call ✦ (steer) | at the next tool call | withdraw | takes it back into the prompt box to edit; Enter then queues it as a normal prompt | Ctrl+↓: back to the head of the queue. Ctrl+↑: nothing |
| ✦ agent prompt / $ command | after the turn | remove | edit in place, Enter saves (as before) | reorder (as before); Ctrl+↑ on the head agent prompt while the agent works makes it a steer |

- **Keys** (`relay::queuenav`, now told how many rows are steers and whether the head can steer):
  ↑ on the empty prompt box selects the **top** row, so a waiting steer comes first; ↑↓ walk the
  whole list; Shift+Delete removes or withdraws the selected row and stays on the row that took its
  place; Esc leaves. Ctrl+Enter on a selected steer interrupts the turn and sends it now (the third
  Enter's path, `sendSteerNow`).
- **Withdrawing** (`withdrawSteer(requestId, Drop|Edit|ToQueue)`): the row greys to
  "withdrawing…" (no ×, not selectable) until the worker answers `steer_removed`; then it goes
  nowhere (Drop), stays in the prompt box as the user's draft (Edit), or is prepended to the queue
  (ToQueue). The worker protocol is unchanged; every path uses `queue_remove`.
- **Races.** If the turn takes the steer first (`steer_delivered`), the transcript line stands and
  the status line says it was too late. For an edit, the copy in the prompt box is cleared if it is
  untouched and kept if the user has already changed it ("your edit is still in the prompt box"), so
  no text is lost either way. If the turn ends first (`steer_returned`), a Drop stays dropped, an
  Edit stays in the prompt box, a ToQueue goes to the head of the queue. If a *selected* steer is
  delivered, the selection moves to the row that took its place; with no row left, an Esc in the
  next two seconds says so instead of stopping the turn (it was meant for the selection — found in
  the live run, where exactly that Esc stopped a turn).
- **Drag.** Steers do not drag: "the next tool call" is a delivery point, not a place in a line. A
  queued row dropped above or among the steers goes as far up as a queued row can: the head of the
  queue, and an agent prompt while the agent works one step further, a steer — the same thing
  Ctrl+↑ on the head does. A command, a Relay-written prompt or an idle agent just gets the head of
  the queue and a status line.
- **For a later remote path:** `Pane::queueRows()` returns `QList<QueueRow>` (`id`, `kind`,
  `preview`, `state`) in delivery order: `running`, `steer:<request id>`, `entry:<queue id>`; kinds
  `running`/`steer`/`agent`/`command`; states `running`, `waiting`, `withdrawing`, `queued`,
  `editing`, `paused`. `Pane::removeRow(const QString &rowId)` removes a queued row or withdraws a
  steer and returns false for an unknown or unremovable row. Nothing calls them from outside yet.
- **Hints:** × on a steer → "Next time: ↑ then Shift+Delete" (`queue.steer.remove.mouse`); a queued
  agent prompt dragged above the steers while the agent works → "Next time: Ctrl+↑ · sends it at
  the next tool call" (`queue.steer.drag`); a drag that only reorders → "Next time: ↑ then Ctrl+↑↓ ·
  moves a queued row" (`queue.reorder.drag`). The queue keys are fixed keys, not Keymap actions, so
  the text is fixed like `queue.remove.mouse`. The strip header's key line follows the selection
  (steer, head, other row, none) and its tooltip spells out the whole list.

Files: `src/Pane.h` (`QueueRowDelegate`, the × in `eventFilter`, `handleSessionEvent` steer events,
`SteerEntry`, `queueRows`, `removeRow`, `withdrawSteer`, `forgetSteer`, `requeueSteer`,
`steerQueuedEntry`, `upgradeLastQueuedToSteer`, `escalateSteerToInterrupt`, `sendSteerNow`,
`sendSelectedSteerNow`, the selection helpers, the queue keys in `handleComposerKey`,
`syncEntriesFromList`, `runningLabel`, `rebuildQueueStrip`), `src/QueueNav.{h,cpp}`,
`tests/queuenav_test.cpp`, `docs/ARCHITECTURE.md`, `docs/AGENT-SESSIONS-PROTOCOL.md` §12.5.

## Evidence

`docs/qa_evidence/2026-09-18-one-queue-list/`: `drive.sh` runs Relay under Xvfb in an isolated
jail against `fake-provider.py` (90 s of reasoning, then a 20 s tool call; every request's user
messages logged in full to `requests.jsonl`) and puts every queue key through one running turn:

- `implementer-01` two steers are the top rows, above three queued prompts.
- `02` ↑ selected the top steer; its text is in the prompt box and the header shows the steer keys.
- `03a`/`03b` Shift+Delete withdrew it ("Withdrawn · the agent never saw it"); the selection is on
  the next steer. The greyed "withdrawing…" state lasts one worker round trip, too short to catch
  in a screenshot here.
- `04`/`05` Enter took that steer back into the prompt box; edited and Enter queued it as a prompt.
- `06`/`07` Ctrl+↑ on the head queued prompt made it a steer; Ctrl+↓ put it back at the head.
- `08` Shift+Delete removed a queued row; `09` a queued row edited in place.
- `10` Ctrl+↑ twice: a row to the head, then to a steer.
- `11`/`12` × on a second steer withdrew it.
- `13`/`14` a queued prompt dragged above the steers became a steer.
- `hints-shown.txt`: the jail's hint counters after shot 14 — `queue.steer.remove.mouse` and
  `queue.steer.drag` were each shown once. The toast itself is not in the screenshots: while the
  agent works, the "thinking · N s" pill sits in the toast's corner (as #Y4GE noted). Toast
  placement belongs to the session reworking `toast`/`placeToast`.
- `15`–`17` the steers delivered at the tool call; the Ctrl+↓ row ran as the next turn; the edited
  steer ran after it as an ordinary prompt.
- `requests.jsonl` + `drive.log`: the withdrawn steers (×, Shift+Delete) and the removed queued
  prompt never reached the model; the taken-back steer reached it only edited, as its own turn.

The run used a build of HEAD plus this change with one QA-only patch in the scratch copy:
`backend/relay_core/keybindings.py`'s `ACTION_ID` must accept `_`, because `ssh.split_same_host`
(#S5SH, committed in c2f6aae) failed that check and stopped the agent from configuring at all.
#S5SH has since fixed it in f4d5f5f (`ssh.splitSameHost`); the patch is not part of this commit.

## QA checklist

- [ ] While the agent works, queue a prompt and press Enter on the empty box: a "↪ next tool call"
      row appears at the top of the list, above queued rows, in the agent colour
- [ ] ↑ on the empty prompt box selects the top row (the steer); ↑↓ walk steers and queued rows
- [ ] Shift+Delete on a selected steer greys it ("withdrawing…"), then it goes; the request log /
      transcript never shows it; the selection is on the next row
- [ ] × on a steer does the same and shows "Next time: ↑ then Shift+Delete" (hints on)
- [ ] Enter, or typing, on a selected steer takes it back: the text stays in the prompt box, the row
      goes; Enter queues it as a normal prompt; it never reaches the model as a steer
- [ ] Ctrl+↓ on a steer moves it to the head of the queue; it runs as the next turn
- [ ] Ctrl+↑ on the head queued agent prompt while the agent works makes it a steer, delivered at
      the next tool call; on a command, or with the agent idle, Ctrl+↑ at the head does nothing
- [ ] Ctrl+Enter on a selected steer interrupts the turn and sends it now
- [ ] Queued rows: ↑ edit / Enter save / Esc cancel / Ctrl+↑↓ reorder / Shift+Delete / × as before;
      a highlighted head still holds the queue
- [ ] Dragging a queued agent prompt above the steers while the agent works makes it a steer and
      shows the Ctrl+↑ hint; a steer cannot be dragged
- [ ] Race: withdraw or edit a steer just as the tool call finishes — the transcript shows it
      delivered, the status line says too late, an untouched copy leaves the prompt box, an edited
      one stays
- [ ] `ctest --test-dir build -R queuenav` passes
