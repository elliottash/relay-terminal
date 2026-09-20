# A pane dragged onto a tab's label joins that tab (#A0SF)

Implemented 2026-09-20. Evidence dir for the QA pass; the owner asked for the
change to land without a test run, so this records what was done and what to
check by hand.

## What changed

All in `src/RelayWindow.h` (plus the two texts below):

- `dragPaneEnd`'s `Edge::TabBar` branch resolves `QTabBar::tabAt` at the
  release point. On a tab's label the pane is `takeLeaf`-ed from its window
  and docked beside that tab's first leaf with `insertBeside(..., Horizontal,
  false)` — the page is made current first, because `insertBeside` sizes the
  newcomer from the page it lands in and a page the `QTabWidget` keeps hidden
  has no size to give it yet — then the page is current, the pane active and
  focused, and the target window raised for cross-window drops.
- Empty tab-bar space and the "+" new-tab button (`tabAt() == -1` there) keep
  the old behaviour: `adoptLeafAsTab`, a tab of its own.
- Dropping on the tab the pane already lives in is a no-op (`page ==
  pageOf(dragged)`), and the old "already its own tab here" guard still covers
  the empty-space drop.
- `dragPaneMove` sizes the drop highlight to `bar->tabRect(tab)` when the
  cursor is over a label, so "into this tab" and "a tab of its own" read as
  two different targets before the release.
- `tabBarIndexAt` helper next to `dropTarget`.
- Texts: the Options "With the mouse…" help row (`src/RelayWindow.h`), the
  pane header tooltip and the header-drag comment (`src/Pane.h`).

## Verification

- `scripts/relay-build`: green (45 s) on 2026-09-20.
- No test run and no live Xvfb pass: the owner asked to skip tests and land
  (22:55). No existing unit test covers pane dragging (only the unrelated
  `relay::board::dropTarget`); the checks below are for the QA pass.
