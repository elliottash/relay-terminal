# Two consoles in one tab reconfigured the helper worker in a loop (#CFG1)

Found on 2026-09-21 in the owner's own log while tracing a different report: `relay.log` held
15,865 `configured` events, about two a second for hours, all for the Switchboard tab's two
consoles (the list page and a card page). The loop predates card #MDL1: `worker.log.1` shows the
same storm at 11:41 UTC, before any of that card's commits.

## Mechanism

`RelayWindow::sendFromConsole` recorded *every* message a console sent as "this console is the
one the worker is briefed for" (`m_tabConsole`) and then went through `helperWorker(page, true)`
→ `startBoardWorker`, which rebuilds `configure` with that console's `context` block and re-sends
it whenever the bytes differ. Every console of a tab gets every event of the worker, and a
console answers `configured` with lines of its own (`route_assist`, its role). So with two
consoles in one tab: configure(A) → both get `configured` → B answers → configure(B) → both get
`configured` → A answers → configure(A) → …

## Fix

Only an `ask` re-points the context and may reconfigure. Every other line goes down the pipe the
worker is already on (`src/RelayWindow.h`, `sendFromConsole`).

## Measured

`drive.sh` is `docs/qa_evidence/2026-09-21-agents-are-consoles/drive.sh` with one change: after
the card page opens (`b02-card`) it waits 25 s, copies the sandbox's `worker.log` out and stops.
Run with the card phase only, once per binary:

| binary | `configured` in the log | peak rate |
|---|---|---|
| main as of 08:28 (`/tmp/claude-1000/altmv/build/relay`) | 654 | 23 per second |
| this fix | 3 | — |

The three are: the worker's start, the card page's first ask, and one reconfigure when the tab's
project was attached. `configured-old.txt` and `configured-new.txt` are the lines themselves.

Also in the log, not chased here: a GUI SIGSEGV at 16:14 UTC closing a Switchboard pane (build
09H.13), in `createAgentConsole`'s destroyed-lambda — `7fce9ef8` (#SWPH) says it fixes exactly
that.
