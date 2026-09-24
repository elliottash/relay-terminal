---
id: PKT5
type: work
status: needs-verification
labels: [bug, remote]
assignee: claude-code
implemented_by: anthropic/claude-opus-5 via claude-code
rank: zpkt5
created: '2026-09-22'
source: Found by Claude Code reviewing the phone app, 2026-09-22
links: {plans: [], commits: [dc2c00ab, 4777602f, 8aa0eb2c, 7d313776, 5f7432db], evidence: [docs/qa_evidence/2026-09-22-phone-ux-drive/, docs/qa_evidence/2026-09-22-streamA-pane/, docs/qa_evidence/2026-09-22-streamD-shell/], related: [PH0N], github: null}
---
# A phone coming back from a pocket keeps a stale pane, and the back button closes the app

## Issue
**1. A reconnect never asks for the pane's state.** `openPane()` sends `pane_focus` **and**
`pane_state_get` (`app/app.js:977-978`); the reconnect path sends only `pane_focus`
(`app/app.js:504`). The hub's `_on_pane_focus` replays the agent ring, a screen snapshot, `control`
and `participants` — not a `pane_state` — and `resume` deliberately cannot replay one
(`remote/host.py:2851`). So a phone that slept through the end of a turn comes back with the old
queue count, the old Stop button and the old model strip, and only an unrelated change fixes it.
An idle pane publishes on change only, and the change already happened while the socket was down.
One `pane_state_get` on the resume path closes it; the hub's `book.latest` already holds the
right answer.

**2. Nothing listens for the back button.** `grep -n "popstate|pushState|history.back" app/*.js`
is empty; the only history calls are `replaceState`. On Android the system Back closes the
installed app instead of closing the open sheet or leaving the thread, and in a browser tab it
leaves the origin — taking the connection, the pane list and the in-memory outbox (a `compose`
typed offline and waiting to flush) with it. Escape and a backdrop tap cover the desktop and a
deliberate tap outside; they are not the gesture an Android user reaches for.

**3. A notification tap onto another pane leaves the old pane's view mounted.** The `open_card`
branch of the service-worker handler calls `closePane()` first; the `open_pane` branch and
`requestOpenPane` do not (`app/app.js:984`), and `openPane` never calls `closePaneView()`. The
title and cwd flip to pane B while pane A's mounted view — and any sheet it has open — stays on
screen; a row tapped in it emits `conversation_open` for pane A. `ensurePaneView()`'s early return
also skips the `openQuestions` hand-off, so B's ask, replayed on `pane_focus` before the first
`pane_state`, is not drawn.

**4. The agent's ask buttons and the queue rows are rebuilt about ten times a second.**
`renderAsk` clears `askChoices` (`app/pane.js:834`) and `renderRows` clears `rows`
(`app/pane.js:444`) on every `draw()`, unguarded — the same failure this file already fixed twice
and documented for the model `<option>`s (`:1036`) and the conversations list (`:1091`). A finger
down on an ask option when a `pane_state` lands has its `pointerup` on a different node, so no
`click` fires and the tap does nothing.

## Done means
A phone that has been asleep comes back showing the pane as it is now — the queue count,
Stop and the strip all match the desktop — without waiting for an unrelated change. The system Back
button closes what is open (a sheet, then the card or thread) before it ever leaves the app. A
notification for another pane leaves nothing of the previous pane on screen. A finger already down
on an ask option or a queue row still activates it when a `pane_state` lands mid-press. It fails if
a resume leaves a stale queue count, or if Back closes the installed app from a thread.

## Execution Summary
All four items are landed, across three commits and two streams — the split is by file, because
item 4 is the pane view's and items 1–3 are the client shell's.

**1. Coming back (`4777602f`).** `openPane` and the reconnect now go through one `refocusPane()`,
which sends `pane_focus` and, under the same `features.includes('pane_state')` guard,
`pane_state_get`. The hub answers from `book.latest`, so a phone that slept through the end of a
turn comes back to the queue count, the Stop and the strip the desktop is actually showing.

**2. Back (`4777602f`, finished in `8aa0eb2c`).** While anything is open the app keeps one spare
history entry. Back pops that instead of the page, `closeOneLayer()` closes the deepest open layer,
and a new spare goes on while anything is still open; with nothing open the spare is dropped, so
Back from the inbox leaves in one press. The board navigates itself, so the spare is reconciled
after any tap (capture-phase listener) and after the service worker's `open_card`.

`closeOneLayer()` asks the mounted pane view and the board whether they have a layer to close, and
neither returned a handle to ask with — both calls fell through their `typeof` guards, so **Back
closed the whole pane out from under an open sheet**, which is a worse answer than not listening
for Back at all: the reader loses the pane as well as the sheet. Those two files belonged to other
streams while this one ran; `8aa0eb2c` closed the gap once they were free. `mountPane` gains
`closeSheet()` and `mountBoard` gains `closeSheet()` and `closeCard()`, each returning whether
there was anything there, because Back has to fall through to the next layer when there was not.
The closing itself already existed inside both files; the answer did not.

**3. A notification onto another pane (`4777602f`).** `openPane` releases the pane it is leaving
through the same `leavePane()` `closePane` uses — the keyboard if this device holds it,
`pane_blur`, and the mounted view with any sheet up in it — and closes the board, which only
thought it was still visible. Dropping the view means the next `pane_state` mounts a fresh one, so
`ensurePaneView` hands over the ask replayed for the new pane on `pane_focus`, which its early
return had been skipping.

**4. The ten-a-second rebuilds (`dc2c00ab`).** `renderAsk` cleared `askChoices` and `renderRows`
cleared `rows` on every `draw()`, unguarded. Both now carry the signature guard their neighbours
already had: the ask on the question's id, which of its questions is showing, whether this device
may answer, and the worker's own words and options; the queue on the selected row and each row's
id, kind, state, label and offered actions. `renderRows`'s `scrollIntoView` was fighting the reader
at the same rate and now runs only when the selection itself moved.

## Tests
`RELAY_KEYRING=off python3 -m unittest tests.test_remote_browser` — 24 tests, OK, and
`… tests.test_pane_view` — 36 tests, OK. Both re-run by the orchestrating session after the
landing. Every test below was also run against the code before its fix and fails there.

**Items 1–3** — real headless Chrome at 390×844 over a real Noise session against a real host:

- `tests/test_remote_browser.py::PocketAndBackTests::test_a_phone_that_slept_comes_back_to_the_pane_as_it_is_now`
  — the host closes the channel, publishes a state while the socket is down (the running prompt
  finishes, two more queue), and the phone shows it after its own reconnect. Without
  `pane_state_get` it times out on the old one row.
- `tests/test_remote_browser.py::PocketAndBackTests::test_back_closes_an_open_sheet_before_it_closes_the_pane_under_it`
  — a Conversations sheet open over a pane: one Back closes the sheet and leaves the pane, the next
  takes the pane. Against a clean `git archive` export of HEAD without the handles it fails where
  it should: `AssertionError: False is not true : Back took the pane away with the sheet`.
- `tests/test_remote_browser.py::PocketAndBackTests::test_back_from_a_thread_lands_in_the_inbox_and_keeps_the_page`
  — a mark on the document survives Back, the URL does not move, the link still says `connected`,
  one screen is drawn; the next Back leaves for `about:blank`, so Back is closed, not trapped.
- `tests/test_remote_browser.py::PocketAndBackTests::test_a_notification_for_another_pane_leaves_nothing_of_the_last_one`
  — a real `navigator.serviceWorker` `open_pane` message for pane-2 while pane-1's view is mounted:
  none of pane-1's rows survive, and pane-2's ask (emitted before the switch) is drawn when its
  state mounts the new view. Before the fix: `3 != 0 : pane-1's queue rows are still on screen
  under pane-2's title`.

**Item 4:**

- `tests/test_pane_view.py::PaneViewTests::test_an_ask_option_and_a_queue_row_survive_ten_clock_states`
- `tests/test_pane_view.py::PaneViewTests::test_the_queue_scrolls_to_a_row_only_when_the_selection_moves`

- `manual: docs/qa_evidence/2026-09-22-streamD-shell/` — `resume-pane-state.png`,
  `back-to-the-inbox.png`, `notification-onto-another-pane.png`.
- `manual: docs/qa_evidence/2026-09-22-streamA-pane/` — `PKT5-ask-across-states.png`;
  `{"sameNode":true,"marked":true}` across ten clock-only states, and the `compose` the held button
  still sent.
