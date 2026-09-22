---
id: PKT5
type: work
status: executing
labels: [bug, remote]
assignee: claude-code
rank: zpkt5
created: '2026-09-22'
source: 'Found by Claude Code reviewing the phone app, 2026-09-22'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-22-phone-ux-drive/'], related: [PH0N], github: null}
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
