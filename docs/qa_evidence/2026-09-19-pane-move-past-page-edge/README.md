# A keyboard move past the page's edge — implementer evidence (#8G7E)

## What changed

`Ctrl+Alt+arrow` on a pane with no neighbour that way no longer refuses with
"No pane in that direction": the pane is carried **past the page's edge** into a
column (left/right) or row (up/down) of its own. The bottom pane of a stack in
the page's rightmost column becomes the whole of a new rightmost column.

- `relay::panes::fillsTheEdge()` — true when the pane already spans the page
  across the direction of travel; the only cases that still notice are the
  tab's only pane and a pane that already has that edge to itself.
- `relay::panes::sizesAfterEdgeDock()` — the newcomer takes one equal share of
  the root splitter; the panes already there keep their relative sizes, the
  widest absorbs the rounding.
- `RelayWindow::movePastPageEdge()` — `takeLeaf` + reinsert into the root's end
  (or start) when the root already runs that way; the root is wrapped in a new
  splitter of the move's orientation when the page runs the other way. The
  shell, the agent and the scrollback travel with the pane.

This landing also carries the PaneLayout half of **#JXWT** (they share
`PaneLayout.*` and `tests/panelayout_test.cpp` and could not land apart):
`PlacementWindow::keyPress()` takes the keymap's `boundAction`, so an arrow with
Ctrl still held from the split key places the pane while that chord is free.

Recovered from the stopped "Moving panes past page edges" session (snapshot
16:36, code complete and tested, never landed); landed by the continue session.

## Verification

- `relay-panes-tests` (`ctest -R panes`): **42 passed, 0 failed** — built
  2026-09-19 17:29 from exactly these sources (binary newer than the last edit
  of `PaneLayout.cpp` 17:28:35 / `panelayout_test.cpp` 17:29:13). Includes the
  new `fillsTheEdge`/`sizesAfterEdgeDock` cases and the placement-with-Ctrl
  cases (bare arrow places; Ctrl/Ctrl+Shift arrow places while unbound;
  Alt+Left and a bound chord keep their shortcut; modifier-only presses keep
  the window open).
- The landing itself went through land.py's gate: the exact tree it put on the
  branch (tip of main + these hunks only) was configured and built, and the
  `panes` ctest case run there — see the commit.

## QA checklist

- [ ] Two panes side by side; focus the right one; `Ctrl+Alt+Right` — it
      becomes a new rightmost column of its own, half the page wide, focus and
      shell travel with it (no "No pane in that direction" notice).
- [ ] A stack of two in the rightmost column; focus the bottom one;
      `Ctrl+Alt+Right` — it becomes the whole of a new rightmost column; the
      column it left keeps its share.
- [ ] `Ctrl+Alt+Left` on that pane walks it back left the same way; the sizes
      it left behind are kept proportionally, not reset to equal shares.
- [ ] The tab's only pane: the move says "This pane is already the only pane in
      its tab." and does nothing.
- [ ] A pane that already fills that edge alone (a full-height column moved
      left again) says "This pane already has that edge to itself."
- [ ] #JXWT: `Ctrl+E` then `Ctrl+Down` (Ctrl still held) places the new pane
      below; `Ctrl+Shift+E` then `Ctrl+Down` too; a preset that binds
      `Ctrl+Shift+Down` keeps the shortcut and does not place.
- [ ] A pane moved past the edge keeps its agent session and scrollback (type,
      scroll, ask the agent).
