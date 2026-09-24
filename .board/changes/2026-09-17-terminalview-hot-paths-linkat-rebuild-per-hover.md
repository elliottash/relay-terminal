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

## Done means
The three redundant recomputations in `engine/view/TerminalView.cpp` are gone and nothing visible changes:

1. Hovering across the cells of one unchanged logical line runs `logicalRowAt` + `links::scan` + the filesystem probe at most once per frame, not once per cell (proved by a counting `setLinkProbe` test); hover underline, click-to-open and rest-colouring behave exactly as before — the existing ViewTest link/hover tests pass unmodified.
2. `paintRow` computes a cell's `CellColors` once per cell per row paint instead of twice; painted output is pixel-identical (`engine/scripts/gui/folds.sh` before/after).
3. `TerminalAccessible::allText()` answers repeat a11y queries between frame updates from a cached string instead of rebuilding and re-splitting the whole viewport per query; returned text is identical.

Failure would show as staleness, not slowness: a hover underline or link target left over after scrolling or new output, wrong cell colours while a search is active, or screen-reader text that lags the screen — any of those means the cache invalidation is wrong.

## Plan
**Goal** — Remove the audit's three redundant recomputations in `engine/view/TerminalView.cpp` (per-hover-cell `linkAt` rebuild, double `colorsFor`, per-query a11y `allText`) with zero visible behaviour change. Note from #6W0Z's measurements (thread, 2026-09-20): all three are below noise today, so this is a cleanup of the asymptotics (long wrapped lines, many search highlights), not a promised speedup. The bar is identical behaviour with the redundant work provably gone.

**Findings** (verified by reading the code, 2026-09-24; line numbers current):

- `LogicalRow` / `logicalRowAt()` — TerminalView.cpp:2049–2075. Joins continuation rows with `out->text += text` per cell (2073): O(line²) QString appends.
- `linkAt()` — 2119. Calls `logicalRowAt` (2295–96) then `links::scan(logical.text, currentDirectory(), …, m_linkProbe ? m_linkProbe : links::systemProbe(), m_cardLookup, …)` (2311–12) on **every hover-cell change** via `updateHover` (1890–1913). The per-*painted-row* sibling `restLinkColumns` (3088–3143) is #6W0Z's territory (its cwd half already landed in b8e91fe3); do not rework it beyond sharing the `logicalRowAt` improvement.
- `paintRow()` — 808. The `colorsFor` lambda (836) linearly scans `line.highlights` (865) and is called at 886 (background pass), 934 (text pass, with a second highlight loop at 924) and 1024 (fold prefix, col 0).
- `TerminalAccessible::allText()` — 233, loops `view()->m_frame.lines` (236); called from the `text`/`characterCount`/line helpers at 177–217, each rebuilding and re-splitting. Only reachable when `QAccessible::isActive()` (change events gated at 718).
- Invalidation anchor: `ViewportFrame` (engine/core/CellTypes.h:156–179) has per-row `dirty` and `full` but **no version counter**; frames arrive in `pullFrame` at TerminalView.cpp:629, and hover invalidation already keys on `m_frame.full`/`m_frame.dirty[row]` at 643–647.
- Test hooks that already exist: `setLinkProbe` (2021; header 156) lets a test count probe calls; ViewTest.cpp covers hover underline across wrapped rows (971), rest colour (1016), folds, rewrap.

**Steps**

1. **Frame version.** Add `quint64 m_frameVersion` to TerminalView, bumped in `pullFrame` (~629) only when `updateFrame` reports a real change. This is the cache key for steps 2–4. (If the pull can be a no-change frame, bumping unconditionally would make the caches never hit — check the `changed` flag already returned at 629.)
2. **linkAt cache + one-pass build.** Member cache `{int row; quint64 version; LogicalRow logical; std::vector<links::Found> found;}`; `linkAt` reuses it when row + version match, otherwise rebuilds. Clear it in `setCardLookup` (2014) and `setLinkProbe` (2021). cwd needs no key of its own — it is resolved per frame since b8e91fe3, so a `cd` arrives with a new frame. In `logicalRowAt`, replace the per-cell `+=` with one `reserve()` (sum the continuation rows' lengths first) and a single append pass.
3. **paintRow: colours once per cell.** Fill a `QVarLengthArray<CellColors>` for the row's cols in one pass at the top of `paintRow` (one walk of `line.highlights`), and index it from both the background (886) and text (934) passes; keep the fold-prefix `colorsFor(0)` use (1024) working.
4. **allText cache.** Cache the joined viewport string (and whatever the split helpers at 204/217 need) keyed by `m_frameVersion`; rebuild only when the version differs. Keep the `QAccessible::isActive()` early-out exactly as is.
5. **Tests** (engine/tests/ViewTest.cpp): a counting-`setLinkProbe` test — hovering across N cells of one wrapped path probes once per frame, and once again after new output arrives; the existing hover/fold/rest-colour tests double as the coloursFor regression net, run them all. If the a11y cache can't be driven without a screen reader, factor the join+split so the cache key is unit-testable directly; do not fake `QAccessible::isActive()`.

**Orchestration** — none. One file, one agent, no subagents.

**Risks**

- Stale cache shows as a stale underline, link target, colours or a11y text — invalidation must be the frame version, never a content comparison. The version must bump only on real change (step 1).
- Sharing the linkAt cache with `restLinkColumns` would need per-row (dirty-flag) invalidation instead of per-frame; only do it if it stays simple, otherwise keep two caches.
- GhosttyCore is untouched (no `VtCore` interface change), so the "can't build GhosttyCore here" constraint from #6W0Z does not apply.
- No measured perf win is promised — if the owner would rather not spend churn on items 2–3 (below noise), say so and the card shrinks to step 2. Default: do all three; each is small.

**Verify**

- Build via `scripts/relay-build`, then `ctest --test-dir build -R ViewTest` (existing link/hover/fold tests unchanged + the new counting tests).
- `engine/scripts/gui/folds.sh` pixel-identical against the pre-change build (the bar #6W0Z used).
- Manual smoke: hover a wrapped path — underline spans every row and clears on leave; `cd` then `ls` still colours paths; a search with many matches paints the same colours as before.
