# Esc to the filter bar (#K9X6) — implementer evidence

Commit: `b15c86e3` (`#K9X6: Esc on the Switchboard main page goes to the filter bar`,
`Implemented-By: glm/glm-5.3`).

## What "done" was taken to mean

On the Switchboard main page (the list page, no card open), pressing Esc puts the keyboard
in the filter bar, ready to type. The steps that already existed keep their precedence, so
Esc always does exactly one visible thing:

1. A card that has the pane goes back first (unchanged).
2. An active filter comes off next (unchanged; same first Esc as inside the box).
3. Otherwise the filter bar takes the focus (`focusFilter()`: focus + select-all).

Esc *inside* the filter still hands the keyboard back to the list when the box is empty
(unchanged), so Esc is not a trap. The rule lives once in `BoardView::handleBoardKey`
(`src/BoardPane.cpp`), which both the list's event filter and the view's own key press
reach — the list's old half-duplicate Esc case was deleted. Standing hint rule: clicking
into the filter with the mouse now hints "Next time: Esc" (`board.filter`, through the
usual `ShortcutHints` gates); the key line at the bottom of the list page reads
"`/` or `Esc` filter"; `docs/SWITCHBOARD-DESIGN.md` 4.4 names both.

## How it was verified

- `tests/boardmodel_test.cpp`: new `escOnTheMainPageGoesToTheFilterBar` covers all of the
  above (list → bar, view itself → bar, active filter cleared first, box's two old steps,
  card back first, and the mouse-click hint firing for `MouseFocusReason` only).
- Suites run offscreen (`QT_QPA_PLATFORM=offscreen`) against a clean `git archive` of the
  landed commit `b15c86e3`, not the shared working tree (which holds other sessions'
  uncommitted work): see `test-run.log`.
  - `relay-board-tests`: **57 passed, 0 failed**.
  - `boardsections`, `boardworkspace` (they link the same `relay-board` library): passed.
- The land.py build gate built target `relay` from the exact merged tree before the
  compare-and-swap.

## Notes for QA

- Worth a live look under Xvfb beyond the offscreen tests: focus arrival in a real window
  (window-activation focus reasons), and Esc while a section checkbox has the keyboard —
  both go through the same `handleBoardKey` path the tests drive.
- The hint text is "Next time: Esc"; its id reaches `RelayWindow::hint()` as
  `board.board.filter` because every board hint already carries a `board.` prefix that the
  window adds again (pre-existing, cosmetic, untouched here).
