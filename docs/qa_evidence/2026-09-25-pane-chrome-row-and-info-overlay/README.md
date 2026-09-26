# Pane chrome row ⓘ ☰ new-pane ×, info overlay, session-ID copy — implementer evidence

Card: #7EWF
Date: 2026-09-25

## What changed

- `PaneChrome` builds the ⓘ itself for a terminal pane (`infoButton()`), then ☰, the painted
  new-pane mark, ×. `RelayWindow::syncChrome()` no longer walks the chrome's layout to insert
  the ⓘ. That walk broke when the chain chip was added above the row, and the ⓘ disappeared.
- The ☰ menu (`PaneChrome::buildMenu`) has Move to new tab, Move to background (terminal
  panes only) and Dim pane / Restore automatic dimming. Each entry shows its live key and runs
  through `onAction`, so picking it shows the same shortcut hint as the old buttons did.
- ⓘ, Alt+I and `/status` toggle `relay::sessioninfo::InfoOverlay`, a child of the pane
  anchored under the ⓘ. It shows `renderSummary`: model, context, tokens, the session ID
  (copyable) with the pane's short ID, started, turns and instructions. It leaves out cost, the
  usage table and the history. The same action, Esc or a click outside closes it. The info pane
  (`InfoView`) remains for the Sessions pane, subagent-thread links and chrome-less consoles.
- The pane header's `auto` badge is gone. A painted copy button after the title
  (`paneSessionCopy`) copies the full Relay session ID. It is hidden while the pane has none.

## Commands and results

- `python3 scripts/land.py try c7ewf` built the exact landing tree (`--target relay`).
- `python3 scripts/land.py try c7ewf --tests '^(conversations|consolemode)$'`:
  - `conversations`: 60 passed, 1 failed. The new `infoSummaryLeavesOutCostAndHistory` and
    `infoOverlayCopiesAndCloses` pass. The failure is
    `theHelperConsoleSitsAtTheBottomCollapsedAndAsksAsTheSessionsPane`, which fails the same
    way on a clean export of `main` (59 passed, 1 failed there).
  - `consolemode`: the four new cases in `tests/paneinfo_chrome_cases.h` pass (row order with a
    chain chip present, menu contents per pane kind and its keys, the menu running `onAction`,
    the header copy button and its clipboard, the overlay answered through the pane's worker
    and closed by an outside click and by Esc). Five other checks fail in the file at lines
    726, 727, 739, 2010 and 2013. The same five fail on a clean export of `main`.

## Screenshots

Live, the landing tree's `relay` under Xvfb with an isolated `XDG_*`/`HOME`:

- `01-row.png`, `01-row-zoom.png`: a terminal pane's row reads ⓘ ☰ new-pane ×. The Models
  tool pane beside it has ☰ new-pane × and no ⓘ.
- `02-menu.png`, `02-menu-zoom.png`: the ☰ menu with Ctrl+Alt+B and Alt+D in the shortcut
  column.
- `03-dimmed-from-menu.png`: Dim picked from the menu dims the pane and shows the hint.
  `03b-menu-when-dimmed.png`: the entry then reads "Restore automatic dimming", ticked.
- `04-info.png`: in that isolated profile no agent provider is configured, so the ⓘ says so.
  `/status` had the same check before this change.
- `05-row-light.png`: the painted glyphs in the `relay-light` theme.

Offscreen, from the consolemode cases with `RELAY_SHOT_DIR` set:

- `info-overlay.png`: the overlay filled from a `session_info` answer, anchored under the
  (test's stand-in) ⓘ, over the pane.
- `header-session-copy.png`: the copy button directly after the title.

## QA checklist

- [ ] In a terminal pane whose linked-pane chain chip is showing, the row still reads ⓘ ☰ new-pane ×.
- [ ] ☰ → Move to new tab, Move to background and Dim each work, and each shows its key hint.
- [ ] ⓘ, Alt+I and `/status` open the overlay under the ⓘ. It has no Cost and no History. A
      second Alt+I, Esc or a click elsewhere closes it, and typing goes back to the pane.
- [ ] Clicking the session ID or its ⧉ in the overlay copies the full ID.
- [ ] The header shows no `auto` badge. The copy icon after the title copies the same session ID
      and shows a toast. A fresh pane with no conversation has no icon.
- [ ] Opening a saved session's info from the Sessions pane still docks the full info pane.
