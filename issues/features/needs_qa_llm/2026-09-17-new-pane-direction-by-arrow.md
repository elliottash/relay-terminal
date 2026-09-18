---
id: 78BN
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (1M context) (Claude Code session), 2026-09-17
rank: zzx
created: '2026-09-17'
acceptance: one key makes a new pane to the right, and pressing an arrow immediately after moves the new pane to that side instead
source: '`issues/feature_intake.txt`, 2026-09-17: "for new pane, make it where, instead of splitting to the right or splitting down, it splits to the right by default, but if your next key stroke is up arrow, left arrow, or down arrow, thats where the new pane goes."'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-17-ux-batch2/'], related: [ERES, 4PW5], github: null}
---
# One key for a new pane, with an arrow to choose the side

## Behaviour

- Ctrl+E (and Ctrl+Shift+E, card #5FY5) makes a new pane **to the right** at once, focused and running.
- While a short window is open (proposed 2 s, and until any other key or a click), Left, Up or Down re-docks
  the new pane to that side; Right keeps it. The pane's shell and agent are never restarted by the move.
- A transient hint under the new pane shows "← ↑ ↓ to place" and disappears with the window.
- Arrow keys typed after the window has closed behave normally (history, cursor movement).
- The separate "new pane below" key is dropped; the palette keeps explicit "New pane to the right / below /
  left / above" actions for people who want them bound.

## Notes
- Reuse the existing pane-move / re-dock code so a moved pane keeps its state, and `#ERES` (pane moves broken)
  should land first.

## Implemented

### The rule, on its own (`src/PaneLayout.h`, `src/PaneLayout.cpp`)

`relay::panes::PlacementWindow` is the whole state machine and has no widgets and no timers: the caller hands
it a monotonic millisecond clock, so every rule is testable.

- `arm(now)` opens the window; `kTimeoutMs` is 2000.
- `keyPress(key, modifiers, now)` answers `Place(direction)`, `Dismiss` or `None`. A **bare** Left, Up or
  Down places; **Right** closes the window and is swallowed too, since it is an explicit "yes, here".
  A modified arrow (Alt+Left already focuses the pane to the left) dismisses rather than places, so
  shortcuts keep doing what they always do. Anything else dismisses, and `Dismiss` means *the caller does not
  consume the key* — so a letter, or a late arrow, reaches the prompt box or the shell as usual.
- A press of Ctrl, Shift, Alt, Meta or AltGr on its own is **not** "any other key": it is the first half of a
  shortcut, so the window stays open.
- `mousePress(now)` closes the window and never consumes the click.

### The window (`src/main.cpp`)

`RelayWindow::splitToward(direction, engine, offerPlacement)` replaces the old `split(orientation)` and makes
the pane on any of the four sides; `pane.splitRight` passes `offerPlacement = true`, nothing else does.

- `armPlacement(pane, anchor)` remembers the new pane **and the pane it was split from**, starts the clock,
  shows the hint and starts one single-shot timer that ends the window. It also re-installs the window's
  application event filter, which moves it to the front of Qt's list, so the arrow is seen before the new
  pane's prompt box could treat it as cursor movement.
- The filter runs at the very top of `RelayWindow::eventFilter`, before anything else, and only needs the
  event to belong to this window — **not** to any particular widget. With no focused widget at all (bug
  #4PW5, being fixed separately) the key still reaches the window, and the arrow still places the pane.
- `placeNewPane(direction)` re-docks relative to the **anchor**, not to whatever happens to be a neighbour:
  `takeLeaf(pane)` then `insertBeside(anchor, pane, orientationFor(direction), towardStart(direction))` —
  the same pair `moveActive` uses when two panes do not already share a splitter (card #ERES), so the pane
  keeps its shell, its agent and its scrollback. Right returns early, since the pane is already there. The
  move runs from a zero-timer, so no pane is re-parented underneath a key event still being delivered.
- The hint is a `toast`-styled label over the new pane, low but clear of the prompt box, clamped inside the
  window, and placed again once the layout has run (a brand-new pane has no geometry yet). One timer owns it,
  so it cannot outlive its window.

### Keys and the palette

- `pane.splitRight` — **Ctrl+E**, **Ctrl+Shift+E** (from commit 9191fa8) — described as "New pane to the right
  (then ← ↑ ↓ places it)".
- `pane.splitDown` keeps its action but **loses every default key**, in the Relay defaults and in all three
  preset tables (`warp`, `vscode`, `konsole`). Ctrl+Alt+E, which commit 9191fa8 had given it, is free again.
- `pane.splitLeft` and `pane.splitUp` are new actions with no default key.
- The palette lists all four: "New pane to the right" (with "Then ← ↑ ↓ within two seconds places it on that
  side"), below, to the left, above. The pane chrome's ⬓+ button still runs `pane.splitDown`.
- Per WARP.md: using the below / left / above action shows a hint the first few times — "Next time: Ctrl+E
  then ↓ puts the new pane there" — built from the live keymap text, never a hard-coded key.

### Tests

`tests/panelayout_test.cpp` (`panes` group, no new ctest group): each arrow's direction and that one arrow
closes the window; a modified arrow and an ordinary key dismiss without placing; modifier-only presses keep it
open and the arrow after them still places; the window is open at 1999 ms and shut at 2000 ms, and a late
arrow is passed on rather than swallowed; a click closes it; `cancel()` closes it.

### Evidence

`docs/qa_evidence/2026-09-17-ux-batch2/`, both engines: `09-new-pane-hint` (the pane on the right with
"← ↑ ↓ to place"), `10-placed-below` (↓ inside the window re-docked it below, same shell and prompt),
`11-late-arrow-ignored` (← about three seconds later did nothing and the hint was gone).

Build: `cmake --build build` with no new warnings. `./scripts/test.sh` 508 passed; `ctest --test-dir build`
16/16.

## QA checklist

1. **Ctrl+E**: a pane appears on the right, running, with "← ↑ ↓ to place" over it. Wait three seconds and
   type into the prompt box: the hint is gone and the text is typed.
2. Ctrl+E then **↓** at once: the new pane moves below the one it came from. Repeat with **←** (to the left)
   and **↑** (above). Each time, the moved pane must keep its prompt, its scrollback and its shell — check by
   running `echo hello` in it *before* pressing the arrow.
3. Ctrl+E then **→**: the pane stays where it is and the hint goes; the arrow must not also move a cursor or
   scroll.
4. Ctrl+E then **Alt+←** at once: the focus moves to the pane on the left (the normal shortcut) and the new
   pane does **not** move.
5. Ctrl+E then hold **Ctrl** for a moment, release, then press **↓** — all inside two seconds: the pane still
   moves below, because a modifier on its own does not close the window.
6. Ctrl+E then a letter at once: the letter lands in the prompt box and the pane stays on the right.
7. Ctrl+E then a **click** anywhere at once: the window closes, the click does what it normally does, and a ↓
   right after it only moves a cursor.
8. Ctrl+E in a pane that is already the right-hand one of a split, then ↑: the new pane must dock above the
   pane it was split from, not somewhere else. Try it with three and four panes in mixed splits.
9. Ctrl+E, then ↓, then Ctrl+E again immediately, then ←: two placements in a row must both work.
10. **Ctrl+Shift+E** must do exactly what Ctrl+E does, including the arrow window, and must work while a
    full-screen program (`vim`) owns the keyboard.
11. **Ctrl+Alt+E must no longer make a pane below** — it is unbound now.
12. Actions palette: "New pane to the right / below / left / above" all work, and using "below", "left" or
    "above" shows the "Next time: Ctrl+E then ↓ …" hint the first few times (Settings › General › Reset
    shortcut hints to see it again).
13. Ctrl+Alt+arrow must still move the focused pane, and Alt+arrow must still only move the focus — neither
    is affected by the placement window.
14. Switch keymap presets (Settings › Shortcuts, or `keybindings.json`): in `warp`, `vscode` and `konsole`,
    the preset's own "new pane" key must also open the arrow window, and no preset may still have a "new pane
    below" key.
15. Repeat 1, 2 and 8 with a KonsolePart pane (`--engine=konsole`), and with a mixed window (one pane of each
    engine).
16. Resize the window very small and press Ctrl+E: the hint must stay inside the window.
