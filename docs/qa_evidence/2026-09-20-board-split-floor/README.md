# Board split floor — #BXCN, implementer evidence (2026-09-20)

**The ask.** "Auto-organize panes" (Ctrl+Alt+0) must make the Switchboard pane wide enough for
its list/card split, and Execute (`x`) / Verify (`v`) from a card must never shrink the board
below that — the other panes give way instead.

**The change.** The board's own 900 px split threshold (`BoardView::updateDetailLayout`) is now
`relay::board::kCardSplitWidth` in `src/BoardPane.h`, shared with the pane layout:

- `relay::panes::sizesAfterEqualize(sizes, floors)` — equal shares, except an entry whose floor
  exceeds its equal share is pinned at the floor and the rest divide what is left; floors whose
  sum eats the total fall back to plain equal shares (`src/PaneLayout.{h,cpp}`).
- `sizesAfterDock(sizes, index, anchorFloor = 0)` — the newcomer still takes its half, but what
  the anchor could not spare above its floor comes proportionally from the panes beside it.
- `RelayWindow::equalizeActivePage` builds the floors per horizontal splitter
  (`boardSplitFloor` = `kCardSplitWidth` + the ToolPane's layout margins, so the *view* really
  gets 900); Execute and Verify pass `boardSplitFloor(guard)` to `insertBeside`, which also
  honours the floor in its wrap path. All other `insertBeside` callers keep the default 0 and
  behave exactly as before. A hand drag can still narrow the board: the floor is what the
  organize/dock arithmetic asks for, not a `minimumSizeHint`.

**Unit tests.** `tests/panelayout_test.cpp`: `dockingKeepsTheAnchorAboveItsFloor` (floor 0 =
unchanged; shortfall paid by the neighbours; anchor already below its floor; sums preserved)
and `equalizingGivesTheBoardPaneItsSplit` (pinning, tidying a wide board back to exactly the
floor, unaffordable floors → plain equal, guards). `ctest --test-dir build -R '^panes$'`:
45/45 pass.

**Live check.** Two harnesses under Xvfb, isolated `HOME`/`XDG_*`, `RELAY_KEYRING=off`, one
fixture card with a `## Plan` (so `x` acts at once):

- `ground-truth-minimal.sh` — board + one terminal, and the readings are the layout Relay
  itself persists (`$XDG_DATA_HOME/relay/state/windows.json` carries every splitter's `sizes`):
  `ground-truth-minimal.txt`. The app sized its window to ~1908–2013 px regardless of the
  2560 request (no WM under Xvfb), which sets the arithmetic:

  | state | saved sizes | reading |
  |---|---|---|
  | board open, card open | `h [1272, 636]` | board second |
  | after Ctrl+Alt+0 (saved only after the next move — see notes) | — | board lifted to 954 (the plain-equal share, floor not binding) |
  | after first Execute | `h [594, 901, 450]` | **board kept 901 (floor 902 + handle), the new pane's ~450 came out of the terminal (1272→594)** — exactly `sizesAfterDock`'s floor branch from a 954-wide board ([527, 904, 477] predicted, Qt handle/rounding the ±70 difference) |
  | after second Execute | `h [391, 818, 409, 395]` | three terminals at their ~400 minimums; 902 + 3×~400 > 2013, so Qt clamps the board to 818 — the documented, unavoidable fallback |

  Both Executes really opened panes: 2 `pane_token` entries in the card's thread. No
  `gui_crash`; Relay alive at the end.

- `drive.sh` — the visual run (OCR + pixel edges, `ocr.txt` + screenshots): Ctrl+Alt+0 with a
  full tab keeps the list and the open card side by side through the reshuffle ("06 equalize …
  yes"); further Executes into an already-full ~1920 tab push past what the window can afford
  and the card stacks — the same Qt clamp as above, not the floor logic.

**Notes / things met on the way (pre-existing, not touched here).**

- `QSplitter::splitterMoved` does not fire for programmatic `setSizes`, so an equalized layout
  is saved only after the next splitter move (or on close); `scheduleSave`'s connect at
  `RelayWindow.h` newSplitter. Behaviour is unaffected — `closeEvent` saves — but a mid-session
  crash could lose an equalize.
- The board-open wrap sizes were 2:1 rather than 50:50 under Xvfb without a WM (the queued
  50/50 reads a pre-layout width); with a WM (and in #HKAP's evidence) it is 50:50.
- The shared checkout's `tests/boardmodel_test.cpp` is mid-edit by another session
  (delete-card work); its `theDeleteKeyAndButtonDeleteTheCardAndTheUndoToastSurvives` hangs and
  aborts independently of this change (28/29 board tests pass; the pane-layout tests all pass).

**Shortcut hints.** No new fast path; the hint rule does not apply.
