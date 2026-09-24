# #MXMG — Board vs Sessions & Projects, visual distinction

Captured 2026-09-24 from the working tree (build/relay of this change), offscreen.

- `sessions-list.png`, `sessions-tokens.png` — from
  `relay-conversations-tests listScreenshot` (`RELAY_SHOT_DIR=.`, `QT_QPA_PLATFORM=offscreen`):
  the Sessions & Projects list with the new bold titles over the muted summary line, tags,
  two-line conversation rows.
- `turns-cell.png` — from `managerGroupsByProjectAndSearches`: the same list grouped.

Board-side captures were not taken (the Xvfb staging pass was cut short when the owner closed
the card out); the Board pane's stage pill, brass selection/header and the stronger banded
header with its glyph watermark are in the build — `src/BoardPane.cpp` (`boardHue`, `stageInk`,
`stagePillFont`), `src/PaneStatus.cpp` (`typeStyle` listPane tint, `listHue`),
`src/PaneChrome.h` (`PaneTypeBand` watermark), `src/Theme.cpp` (board header active brass).
The ai-visual pass should take the side-by-side pair from the landed build.

Known unrelated failure while landing: #9ESY (conversations dropdown test fails at HEAD).
