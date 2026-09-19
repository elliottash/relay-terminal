# Diff adds and removals: ink on the fill (2026-09-19)

Owner: "for adds and deletions in diffs, make it black/white text with a green or red background,
rather than green / red text".

Every surface that draws a diff now paints an add line as pure black or pure white text on the
theme's own success green, and a remove line as the same ink on the theme's error red:

* the diff pane (`src/DiffView.cpp`) — the fill is the line's block background, the ink is
  `theme::contrastInk(fill)`, the +/- marker alone stays bold;
* the terminal's inline diff under a tool row (`Pane::inkCode`) — the fill rides along as a
  24-bit SGR background, since the terminal cannot recolour its scrollback;
* the terminal's tool-call folds and the Activity pane's (`calllines::Palette`, filled by
  `Pane::foldPalette()` and `AgentInternalsView::palette()`) — `addBg`/`removeBg` are now the
  tokens themselves, `add`/`remove` the ink that reads on them;
* the turn-details log (`src/TurnTranscript.cpp`) and the subagent transcript
  (`src/SubagentTranscript.cpp`) — the same pair as text formats.

`theme::contrastInk()` picks black or white by WCAG relative luminance: black on a dark theme's
pastel green (#7ec88c → 10.5:1), white on a light theme's deep green (#1e763b → 5.7:1). Whichever
it picks clears 4.5:1 by construction, because the crossover is the one fill where the two inks
score alike.

## Evidence

1. `diff-<theme>.png` (5) — the real `DiffView`, switched live through every shipped theme under
   Xvfb (`drive.sh`, isolated `XDG_CONFIG_HOME`). A pixel check over the PNGs confirmed each
   theme's green and red bands are present (17–34 rows each) with black glyphs on them in the
   dark themes and white glyphs in the light ones.
2. `tests/diffview_test.cpp::addAndRemoveLinesAreInkOnTheirFills` — the add/remove blocks carry
   `theme::Success`/`theme::Error` as background and exactly `contrastInk(fill)` as foreground.
3. `tests/theme_test.cpp::diffFillsTakeABlackOrWhiteInkThatReads` — for every shipped theme,
   `contrastInk` returns black or white and the chosen ink clears AA (4.5:1) on that theme's
   success and error fills.
4. `ctest --test-dir build` green except the pre-existing `buttonfit` failure (#QAJQ,
   `tabProjectChip` 8.5pt, another session's uncommitted stylesheet edit — no font rule is
   touched here); `./scripts/test.sh` green (3,413 tests).
