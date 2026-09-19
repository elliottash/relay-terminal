# QA evidence: Ctrl+E then Ctrl+arrow places the pane (card #JXWT)

Implementer evidence (Relay, 2026-09-19). Screenshots are prefixed `implementer-`; they are not a
QA verdict. The QA session should re-run `drive.sh` itself and compare `results.txt`.

## What changed

`PlacementWindow::keyPress` (`src/PaneLayout.cpp`) accepted only a bare arrow. After Ctrl+E the
hand is still holding Ctrl, so the natural Ctrl+Down dismissed the window and did nothing. Now an
arrow with Ctrl held (Shift may ride along — Ctrl+Shift+E leaves both down) also places the pane,
**as long as the keymap leaves that chord unbound**: `RelayWindow`'s event filter passes
`Keymap::match(key)` in as `boundAction`, and a bound chord keeps doing what it always does —
Alt+arrow focuses, Ctrl+Alt+arrow moves, and a `pane.focusDown` bound to Ctrl+Shift+Down (the
konsole preset) still focuses.

## How to re-run

    docs/qa_evidence/2026-09-19-ctrl-held-placement/drive.sh

Needs Xvfb, xdotool and ImageMagick; runs two Relay instances on display :94 (override with
RELAY_QA_DISPLAY) in throwaway XDG profiles. The assertion channel is the pane's own shell: after
each placement the focused pane reports `$COLUMNS $LINES` into a file in its working directory —
side by side the new pane is narrow (70 cols), placed below it is wide and half as tall
(148 cols × 15 lines), and a split that never happened reads full size (148 × 40+), so a dead
split cannot pass for a placement.

## Results (implementer run)

All eight checks passed; `results.txt` has the grid sizes. Phase A, default keymap:

| # | gesture | expected | got |
|---|---|---|---|
| 01 | Ctrl+E, bare Down | below (regression check) | below |
| 02 | Ctrl+E, Ctrl+Down | below — **the fix** | below |
| 03 | Ctrl+E, Ctrl+Shift+Down | below (the Ctrl+Shift+E twin) | below |
| 04 | Ctrl+E, Ctrl+Right | stays right | right |
| 05 | Ctrl+E, Alt+Down | dismissed: focus chord keeps its meaning | right |
| 06 | Ctrl+E, wait out the window, Ctrl+Down | passed on, stays right | right |

Phase B, `pane.focusDown` bound to Ctrl+Shift+Down (the chord the konsole preset collides with):

| # | gesture | expected | got |
|---|---|---|---|
| 07 | Ctrl+E, Ctrl+Shift+Down | dismissed: the bound chord wins | right |
| 08 | Ctrl+E, Ctrl+Down | unbound in that setup: still places | below |

## Unit tests

`tests/panelayout_test.cpp`: `placementAcceptsArrowsWithCtrlHeld` covers Ctrl and Ctrl+Shift
placing when the chord is free, bound chords (konsole's Ctrl+Shift+Down, a hand-bound Ctrl+Down)
dismissing, and Ctrl+Alt / Shift-only / Alt never placing. The existing placement tests were
updated for the new `boundAction` parameter and all pass (`build/relay-panes-tests`, 42/42).

## Other arrow pane controls checked (the second half of the card)

- **Dock-beneath chord (#Q7Y9)**: Ctrl+Alt+Left/Right then the Move-down action (Ctrl+Alt+Down).
  Goes through the keymap, so holding Ctrl is inherent to it; `chordKeyKeepsWindow` keeps the
  window open for held modifiers. Unchanged, still works.
- **Focus panes**: Alt+arrows. Holding Ctrl turns it into Ctrl+Alt+arrow = Move — deliberate and
  documented (`src/PaneLayout.h`), not a regression.
- **Output-link walk** (Ctrl+Shift+L, then arrows): the event filter takes the four arrows
  regardless of modifiers, so holding Ctrl does not leave the walk.
- **Konsole preset**: Ctrl+Shift+arrow focus keys keep their meaning inside the placement window
  (check 07 above).
