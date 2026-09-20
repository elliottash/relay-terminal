---
id: 9MYY
type: work
status: planned
labels: [bug]
rank: zzz
created: '2026-09-17'
source: Relay pane, cleanup audit 2026-09-17
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# TerminalView hot paths: linkAt rebuild per hover cell, colorsFor twice per cell, a11y allText

## Request
look for cleanup opportunities

## Findings
From the 2026-09-17 cleanup audit (verified by reading the code; engine/view/TerminalView.cpp):

1. **`linkAt()` (~1120), MEDIUM** — on every hover-cell change it rebuilds the joined logical line cell-by-cell with `logical.text += text` (O(line²) QString appends, one allocation per cell), then runs `links::scan(...)` with `links::systemProbe()` — `stat()` syscalls per candidate path. Fix: cache the built line keyed by row (invalidate via frame dirty flags), build with `reserve()` in one pass, stat only the candidate under the cursor.
2. **`paintRow()` (~500), LOW/MEDIUM** — `colorsFor(col)` is computed twice per cell (background pass and text pass), each call also linearly scanning `line.highlights`, so painting is O(cols × highlights) twice per row with an active search. Fix: compute colors once per cell into a stack array; precompute highlight ranges per row per frame.
3. **`TerminalAccessible::allText()` (~100), LOW** — rebuilds the whole-viewport string (and re-splits it) per a11y query; only affects screen-reader users (`QAccessible::isActive()` guard exists). Fix: cache the joined text per frame version.

Not examined: GhosttyCore.cpp (46 KB, deserves the same `updateFrame` review LibVtermCore got — LibVtermCore's looked reasonable).
