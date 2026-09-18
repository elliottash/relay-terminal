---
id: Y4GE
type: work
status: needs-qa-llm
labels: [change, bug]
component: [gui, worker]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: 'Text highlighted in the reasoning bubble while it streams stays highlighted and Ctrl+C in the prompt box copies it (Copy on select copies on release); a steer waiting for the next tool call has a × that withdraws it before the model sees it; `tests/test_queue.py` and `tests/test_remote_wire.py` pass'
source: 'owner, bug intake 2026-09-18 (two items)'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-thinking-copy-and-steer-withdraw/'], related: [], github: null}
---
# Copy from the reasoning bubble; withdraw a steer that is still waiting

## Issue

highlight text to copy doesnt seem to work in the agent's thinking bubble.

you should still be able to cancel a task that is queued for the next tool call, if its still waiting.

## Change

**Reasoning bubble** (`Pane::appendThinking`, `copyThinkingSelection`). Three things stood in
the way of copying:

- Every streamed chunk set the view's scroll bar to the bottom, so a drag-selection jumped away
  under the pointer while the model was still thinking. The view now follows the text down only
  while it is at the bottom, nothing is selected and the left button is not held over it.
- The view has `Qt::NoFocus` (the prompt box keeps the keyboard, on purpose), so Ctrl+C never
  reached it. Ctrl+C in the prompt box with nothing selected there now copies the bubble's
  selection, with the same "N characters copied" toast as the terminal.
- Copy on select (Settings › Terminal) now covers the bubble as well as the terminal.

**Withdrawing a steer** (`TurnSupervisor.remove`, `Pane::withdrawSteer`). The "↪ next tool call"
row in the queue strip had no way out except the third Enter, which *interrupts* the turn. It now
has a × like queued rows. The GUI sends `queue_remove {item}` with the steer's item id. The worker
drops the steer if the turn has not taken it, marks its ledger entry `cancelled_by_user`, and
answers `steer_removed`. If the turn took it first, the transcript already shows it
("↪ at the next tool call") and the status line says it was too late. If the turn ended just as
× was clicked, the steer stays withdrawn instead of coming back as a queue item. A × clicked before
the worker has acknowledged the steer is sent as soon as it does. `steer_removed` is forwarded to a
paired phone like the other steer events (`remote/wire.py`); protocol text in
`docs/AGENT-SESSIONS-PROTOCOL.md` §12.5.

## Evidence

`docs/qa_evidence/2026-09-18-thinking-copy-and-steer-withdraw/`: `drive.sh copy|select` runs Relay
under Xvfb in an isolated jail against `fake-provider.py`, a streaming local model that reasons for
20 s and then runs a 30 s command.

- `implementer-copy-01`: a selection made mid-stream is still in place 2.5 s later, while later
  sentences have streamed in underneath it. `copy-clipboard.txt` holds exactly the highlighted text
  after Ctrl+C in the prompt box (the clipboard held `EMPTY` before).
- `implementer-copy-03` / `04`: the steer row with its ×, then gone after the click while the turn
  keeps reasoning. `copy-requests.jsonl`: no request to the model carries `WITHDRAWME`, and the
  steer did not come back as a turn of its own.
- `select-clipboard.txt`: with Copy on select on, the drag alone filled the clipboard.
- `tests/test_queue.py`: `test_remove_withdraws_a_steer_the_turn_has_not_taken`,
  `test_remove_refuses_a_steer_already_delivered`.

## Not done here

- The phone app (`app/app.js`) shows steers from `queue_changed` but has no × for one. That file
  has another session's uncommitted work in it.
- A withdraw has no keyboard path. Queue rows are reached with ↑ from the prompt box, but steer
  rows are not in that list, and whether ↑ should reach them (and what Shift+Delete means there)
  is a product decision.
- The "Withdrawn" toast is usually hidden behind the "thinking · N s" pill that shares its corner.
  The row disappearing is the visible confirmation.

## QA checklist

- [ ] While the reasoning bubble streams, drag across its text: the highlight stays put as more
      text arrives, and Ctrl+C in the prompt box copies exactly that text
- [ ] Scrolled to the bottom with nothing selected, the bubble still follows new reasoning down
- [ ] With Settings › Terminal › Copy on select on, releasing the drag copies without Ctrl+C
- [ ] Ctrl+C with text selected *in the prompt box* still copies the prompt box's selection
- [ ] Queue a prompt while the agent works, Enter again on the empty prompt box: the
      "↪ next tool call" row has a ×; clicking it removes the row, and the prompt never reaches the
      model or the queue
- [ ] × clicked after the steer was delivered: the transcript shows it delivered, and the status
      line says it was too late; nothing is stopped
- [ ] `PYTHONPATH=backend python3 -m unittest discover -s tests -p 'test_queue.py'` passes
