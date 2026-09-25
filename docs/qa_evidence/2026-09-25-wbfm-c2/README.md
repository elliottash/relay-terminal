# #WBFM — pick order headers + rank clarity, effort tab density (Track C2)

## What changed (src/ModelPicker.cpp)

**Pick order tab — class headers (`addClassHeader`)**
- The `high` / `main` / `flash` header rows are now taller than the rows they
  introduce (font height + 18px vs the default 26px row), the class name is
  bold **and one point larger**, the note ("new panes start on rank 1") recedes
  to the muted disabled-text colour, and the band background is unchanged.
- The header's rank cell now carries a right-aligned muted count — `3 ranked` —
  over the numbers that restart at 1 below it, so the column labels itself.
  Headers still consume no rank number.
- `rebuild()` now turns `uniformRowHeights` off on the sectioned pages
  (`kClasses`, `kEffort`) so the taller header doesn't inflate every row (that
  default-on uniformity is what made header and rows one indistinct 26px grid).

**Pick order tab — rank numbers (`buildTier`)**
- Kept: per-tier 1-based numbers, right-aligned, showing the stored rank so a
  tie honestly reads as two equal numbers (equal ranks draw randomly — the
  page's own help line and the cell tooltip say so). Consumers checked before
  deciding: `curation::tierList` lists are independent per tier
  (`ModelRows.cpp` `tierEntriesOf` walks one list; JobsTab sorts by rank and
  groups equal ranks as one draw), so numbering *restarts per tier*, which is
  what the headers now make visible.

**Effort tab (`decorateEffortRow`)**
- Row heights came from padded constants (48px fixed, 66px with a selector);
  they are now sized from the font (two text lines: `lineSpacing * 2 + 6`,
  selector rows at least `combo sizeHint + 4`) → 44px on this theme, vs 26px
  pick-order rows. The two-line model cell ("supports: low · high · max") is
  kept — `tests/modelpicker_test.cpp:532,563` pin that text and tests/ is
  outside this card's claim, so removing the second line was not an option.

## Evidence

- `before-pick-order.png` / `after-pick-order.png`, `before-effort.png` /
  `after-effort.png`, plus `compare-*.png` side-by-side montages.
- `drive.cpp` + `drive.sh` build a `ModelsPane` on fixture presets (same
  fixtures as `tests/modelspane_test.cpp`), seed high/main/flash lists (high
  ties two models at rank 1), and grab both tabs under `xvfb-run -a` with an
  isolated `XDG_CONFIG_HOME` and `RELAY_WORKSPACE`. `WBFM_DUMP=1` prints the
  rendered tree rows (text, flags, heights, band colour).
- Measured: pick-order headers 26→38px with rows staying 26px; effort rows
  66→44px (selector) and 48→44px (fixed); header rank cell `3 ranked`.

## Tests

- `relay-modelpicker-tests`: 62 passed, 0 failed.
- `relay-modelspane-tests`: 25 passed, 1 failed —
  `theHelperIsOneRowUnderAllFiveTabsAndBuildsOneConsole` expects
  "Helper Agent (Alt+Q)" but `ModelsPane::updateHelperRow` (commit e6febb9f,
  #E8V1) says "Agent (Alt+Q)". Pre-existing at tip, unrelated to this card.
