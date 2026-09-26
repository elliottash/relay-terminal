---
id: 9MYY
type: work
status: needs-verification
labels: [bug]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 131947ca-655f-4eef-8401-b4970c2f0a2d
rank: zzz
created: '2026-09-17'
verify: {artifact: code, primary: script, also: [pairwise], human: optional, criteria: relay-engine-tests passes incl. the new hover probe-count test; before/after golden grabs are byte-identical; a person may hover a wrapped path and run a search to confirm nothing looks different, sign_off: none, effort: low}
source: Relay pane, cleanup audit 2026-09-17
links: {plans: [], commits: [c8f6d92116d6, 9ddfbc8cf31b, 2febbf48ad84, 568de5c88448], evidence: [docs/qa_evidence/2026-09-25-9myy-view-hot-paths/], related: [], github: null}
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
**Goal:** remove the redundant per-event work in `engine/view/TerminalView.cpp` with no visible change. That work is the per-hover-cell `linkAt` rebuild and scan (with its core-mutex and `readlink` round trips), `colorsFor` computed twice per painted cell, and the accessibility `allText()` rebuilt and re-split on every query. #PF4K/#6W0Z measured all three as small today (thread, 2026-09-20: 0.141 ms and 4–5 syscalls per hovered cell; `colorsFor` about 3.4 % of GUI samples). So this fixes how the cost grows with long wrapped lines, many search matches and screen readers; it does not promise a speedup. The bar is identical behaviour, with the removed work proved by counters.

**Findings** (re-checked 2026-09-25 against `8163b4ed`; all three are still present; line numbers are today's):

- **Hover → `linkAt`.** `updateHover` (1891) returns early only when the cell is unchanged (1899). Otherwise it calls `linkAt(c, …, &segments)` (1914; `linkAt` body at 2120). Every new hover cell on an emulator row pays for:
  - a `withCore` call → `core.hyperlinkAt` (2222). This takes the core mutex, which the pty thread holds while it feeds output.
  - `logicalRowAt` (2057–2077). It builds a temporary `QString` per cell via `cellText` (2071), appends it (2074), and does a `cellOf.push_back` per UTF-16 unit. That is O(L) with a heap temporary per cell, not the O(L²) the audit said, because `QString +=` grows amortised.
  - a linear search of `cellOf` for the hovered cell (2299–2304), O(L).
  - `links::scan(logical.text, currentDirectory(), …)` (2312). `currentDirectory()` is the uncached path (readlink of `/proc/<pid>/cwd` plus the core mutex), not the per-frame `frameDirectory()` (1973) that #6W0Z gave `restLinkColumns`. `scan` probes every candidate on the logical line, not only the one under the pointer, so an `ls` row with 30 names costs about 30 `stat`s per hovered cell.
  - Fold rows take a separate branch that runs its own `links::scan(text, currentDirectory(), …)` (2176).
  - For a line of L characters, hovering across it costs about L × (1 mutex + 1 readlink + O(L) build + candidates × stat). Nothing on that line has changed in between.
- **`paintRow`** (809). The lambda `colorsFor` (837–877) does 2 × `resolve`, a linear walk of `line.highlights` (865) and `faintInk`. It runs in the background pass (887), again in the text pass (935), and a third time at the fold chevron (`colorsFor(0)`, 1025). The text pass also walks `line.highlights` a third time through `highlighted(col)` (924–928) on rest-link cells. Cost per row: about 2·cols·(H + 2 resolves), plus cols·H on link cells, where H is the number of highlights on that row (non-zero only while a search is active; the #PF4K case had 15,695 matches).
- **`TerminalAccessible`** (≈160–240). `allText()` (234) joins every `m_frame.lines[].text()` into a new string on each call. `text(Value)` (178), `text(start,end)` (200) and `characterCount` (201) each call it, and `characterRect` (205) and `offsetAtPoint` (218) also `split('\n')` the result. A screen reader walking characters therefore costs O(viewport) **per character**. This only happens when a screen reader is attached: change events are gated by `QAccessible::isActive()` at 719.
- **What a cache can key on.** `ViewportFrame` (engine/core/CellTypes.h:157–178) has `dirty`/`full`/`scrolledBy` but no version number. Every write to `m_frame` happens inside `pullFrame`'s `withCore` block: `updateFrame` (630) and `syncFoldViewport` (3634–3697), and that one sets `*changed`. `pullFrame` returns at `if (!changed)` (641). Hover already resets on `full`/`dirty`/`m_visualTopMoved` (644–657).
- **Things that already exist and help.** `frameDirectory()` (1973) is reset in `pullFrame` (615) and `linkProbeUpdated` (2029). `setLinkProbe` (2022; header 156) lets a test count probe calls. `m_restLinks` (h:577) is the text-keyed per-row cache that `restLinkColumns` (3165) uses; it keeps spans only, not `links::Found`. Engine tests are **one** executable, `relay-engine-tests`. `RELAY_ENGINE_TEST=ViewTest` runs only the `ViewTest` object (engine/tests/main.cpp:23), and existing grab hooks use env vars (`RELAY_HOVER_EVIDENCE`, ViewTest.cpp:1008).
- **Adjacent code, out of scope.** 3739 is a second `hyperlinkAt` under `withCore`, in the keyboard link walk; it is not on a hot path. GhosttyCore is untouched, because no `VtCore` interface changes.

**Steps.** All of this is in one file plus its header and tests, so one agent works through it in order, with no parallel subagents. Steps 3, 4 and 5 are independent once step 2 has landed.

1. **Tests and measurement first, landed as their own commit before any change.** This gives before and after the same test source. In `engine/tests/ViewTest.cpp`:
   - (a) `hoverSweepProbesOncePerFrame`: print one wrapped line that holds several path candidates. Install a counting `setLinkProbe`, send mouse moves across N cells and record the probe count. Today it is about N × candidates. After step 3 it must be ≤ candidates, and it must rise again after new output is fed.
   - (b) `paintGrabGolden`: with `RELAY_PAINT_GOLDEN=<dir>` set, save `grab()` PNGs of fixed scenes to that directory. The scenes cover: a search with many highlights including the current one, a selection, reverse video, a wide character, rest-coloured links, a banded role row, and a fold chevron. With the variable unset, the test only checks that painting succeeds.
   - (c) `benchHoverSweep` and `benchPaintHighlights`: `QBENCHMARK` slots that `QSKIP` unless `RELAY_VIEW_BENCH=1`. They sweep the pointer across a 2,000-character wrapped line, and repaint a page with about 200 highlights per row.
   - Record the probe count, the PNGs and the bench numbers from this commit under `docs/qa_evidence/<date>-9MYY/before/`.
2. **Frame version.** Add `quint64 m_frameVersion` to `engine/view/TerminalView.h`, and increment it in `pullFrame` just after `if (!changed) return;` (641). It must never be bumped unconditionally, or every cache would miss.
3. **Hover `linkAt`** (TerminalView.cpp):
   - (a) At 2312 and 2176, use `frameDirectory()` instead of `currentDirectory()`, which is the same rule #6W0Z applied.
   - (b) Add a one-entry member cache `{quint64 version; int firstRow; links::Mode mode; LogicalRow logical; std::vector<int> idxOfCell /*row-major, -1 when none*/; std::vector<links::Found> found;}`. On a hit, the hovered index is a table lookup instead of the loop at 2299. Recompute `segments`, `startCol` and `endCol` on every call, because they depend on `screenRowOfReal` and visual top, not on the text.
   - (c) Clear the cache in `setCardLookup` (2013), `setLinkProbe` (2022) and `linkProbeUpdated` (2029), and whenever `m_frameVersion` changes.
   - (d) For `hyperlinkAt` (2222): when the cell's `link` id is 0, skip the `withCore` call. When it is non-zero, answer from a per-frame id→URI memo, cleared in `pullFrame` alongside `m_frameProse`. An id's URI never changes; `proseLink` (2363) already relies on this.
   - (e) In `logicalRowAt`, `reserve()` `text` and `cellOf` from the summed row widths, and append the `ch` of single-code-point cells directly without building a temporary `QString`. `restLinkColumns` gets this for free.
4. **`paintRow`: colours once per cell.** Before the background pass, fill a `QVarLengthArray<CellColors, 256>` plus a `highlighted` bit per column. Instead of a scan per column, walk `line.highlights` once, painting its ranges onto a per-column index. Later highlights must still win, as the current loop does, so the current match is not overwritten. Read that array at 887, 935 and 1025 and in the `highlighted(col)` check at 926. Keep `colorsFor`'s body as the single function that fills the array, so its rules are not duplicated.
5. **`allText` cache.** Give `TerminalAccessible` a `mutable` cache `{quint64 version; QString text; QVector<int> lineStart;}` keyed on `view()->m_frameVersion` (the class is already a friend, h:289). `text`/`characterCount` return the cached string. `characterRect`/`offsetAtPoint` use a binary search on `lineStart` instead of `split`, and `cursorPosition` (188) uses `lineStart` too. Leave the `isActive()` gate at 719 as it is. For the test, split out the join/offset computation as a free function over a `ViewportFrame` and test it directly. Do not fake `QAccessible::isActive()`.
6. **After the change:** run 1(a)–(c) again into `docs/qa_evidence/<date>-9MYY/after/`, then compare the PNGs byte for byte (`cmp`) and the numbers.

**Risks** (failures here show up as stale output, not as slowness):

- If the version stops bumping on some path that mutates `m_frame`, the underline, link target or a11y text goes stale. Today every mutation is inside `pullFrame`'s `withCore`, including fold viewport shifts. Test 1(a) must cover output, scroll, resize and opening a fold.
- A file created while the screen is idle stays unlinked on hover until the next frame. That matches what rest colouring already does. `linkProbeUpdated` still clears it when the host vouches for a path.
- The `cd` case is the same trade #6W0Z already accepted: the prompt that follows a `cd` is itself a new frame.
- Step 4 must keep "last highlight wins" and the selection → highlight → faint order exactly. The golden PNGs are what enforce this.
- A per-frame URI memo relies on the core never reusing a link id for a different URI within one frame. Confirm this in `LibVtermCore`/`GhosttyCore` `hyperlinkUri` before doing 3(d). If it cannot be confirmed, drop 3(d) and keep only the id == 0 shortcut.
- **Owner question 1:** items 2–3 (paint, a11y) measured below noise. Do all three (my recommendation: each is small, and the a11y fix removes quadratic behaviour for screen-reader users), or cut this card to step 3?
- **Owner question 2:** is it acceptable to land step 1's golden-grab and benchmark slots permanently in ViewTest (env-gated, skipped by default)? My recommendation is yes: they are how the next paint change proves itself.

**Verify**

- Build with `scripts/relay-build --target relay-engine-tests`, then run `ctest --test-dir build -R relay-engine-tests` (the whole engine suite, about 2 minutes). For the fast loop, run `RELAY_ENGINE_TEST=ViewTest build/engine/relay-engine-tests`. Because the change touches a header, also run `scripts/relay-build` (`relay`), since `land.py`'s build gate compiles it.
- Test 1(a) passes. Its probe count drops from about N × candidates to ≤ candidates per frame, which is the Done-means counter.
- `cmp` finds every before/after golden PNG identical.
- `RELAY_VIEW_BENCH=1 RELAY_ENGINE_TEST=ViewTest build/engine/relay-engine-tests benchHoverSweep benchPaintHighlights` before and after, with the numbers recorded in the evidence folder.
- Manual smoke on a real pane:
  - Hover a wrapped path: the underline spans every row and clears on leave.
  - `cd` then `ls`: paths still colour and open.
  - Search with many matches: same colours as before.

## Decisions
Owner, 2026-09-25: “yes, send to subagent and give me the next card”. Approves the recommendation: do all three fixes (hover `linkAt`, `paintRow` colours, a11y `allText`), and keep the golden-grab and benchmark slots permanently in ViewTest (env-gated, skipped by default).

## Tasks
- [x] 1. Tests/measurement first, own commit: `hoverSweepProbesOncePerFrame`, `paintGrabGolden` (behind `RELAY_PAINT_GOLDEN`), `benchHoverSweep`/`benchPaintHighlights` (behind `RELAY_VIEW_BENCH`); before numbers, PNG, probe counts in `docs/qa_evidence/2026-09-25-9myy-view-hot-paths/before/` <!-- t:r2 -->
- [x] 2. `quint64 m_frameVersion`, bumped only in `pullFrame` after `if (!changed) return;` <!-- t:rj -->
- [x] 3. Hover `linkAt`: `frameDirectory()`, one-entry per-frame row/scan cache with `idxOfCell`, cache dropped on probe/card-lookup/frame changes, `hyperlinkAt` skipped when the id is 0, per-frame id→URI memo (ids confirmed not reused within a frame), `logicalRowAt` reserve + no per-cell temporaries <!-- t:bd -->
- [x] 4. `paintRow`: per-cell `CellColors` + `highlighted` once per row paint; `colorsFor` stays the single source of the colour rules <!-- t:xq -->
- [x] 5. `TerminalAccessible`: joined text + `lineStart` offsets cached on `m_frameVersion`; `split` replaced by binary search; join/offset helper tested directly; `QAccessible::isActive()` left honest <!-- t:dx -->
- [x] 6. After-run into `docs/qa_evidence/2026-09-25-9myy-view-hot-paths/after/`; every golden PNG byte-identical under `cmp` <!-- t:h1 -->

## Execution Summary
Commits: c8f6d921 (tests/measurement first), 9ddfbc8c (frame version), 2febbf48 (hover linkAt cache, paintRow colours once, accessibleText cache).

- Hover: one logical-row build and one links::scan per frame version; hyperlinkAt skipped for id 0; OSC 8 URIs memoised per frame; frameDirectory() instead of currentDirectory().
- paintRow: CellColors filled once per cell into a QVarLengthArray from colorsFor, read by the background pass, text pass and fold chevron.
- Accessibility: free function accessibleText(frame) → joined text + lineStart; cached on m_frameVersion; rowOf() binary search replaces split.

The resumed subagents (a3, then a5) wrote steps 3–6 and the after-run evidence but failed before landing; the parent session landed them unchanged after building the exact tree.

## Tests
- `land.py try phone-9myy --target relay-engine-tests --tests ^relay-engine-tests$` on tip f8359d95 + these hunks: builds, ctest passes. land.py's commit gate then built `relay` on the same tree.
- after/evidence.md: hover sweep 1232 → 14 probe calls per 88-cell sweep; benchHoverSweep 1.6 → 0.014 ms; benchPaintHighlights 0.11 → 0.093 ms.
- `cmp before/paint-libvterm.png after/paint-libvterm.png`: identical (re-checked by the parent session).
- Not done: the manual smoke on a real pane (hover a wrapped path, cd + ls, search with many matches) — left for the verifier.
