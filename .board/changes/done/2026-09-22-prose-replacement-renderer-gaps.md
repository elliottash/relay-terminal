---
id: RW9T
type: work
status: done
labels: [bug, terminal]
assignee: claude-code
rank: m
created: '2026-09-22'
source: 'Claude Code in a Relay pane, 2026-09-22'
links: {plans: [], commits: ['8164a9e702a2c6b1b790c5b6f0a0fc6e8ec89db6'], evidence: [], related: [HCR2, R2WQ, K9KC], github: null}
---
# Three more gaps in the prose replacement renderer

## Issue
were there any other bugs introduced there

yes, fix all

(Asked after #HCR2 traced the hash-link colour regression to the prose replacement renderer
added with #R2WQ. The three defects found by that audit are below.)

## Done means
- A find over a reply that a resize has re-wrapped reports each match once, not twice: the
  label reads the same count at the print width and away from it. Failure shows as an inflated
  "n of N" in the find strip whenever a prose block is taken over.
- `~~struck~~` text in agent prose keeps its line through a resize. Failure shows as the rule
  disappearing at any width other than the one the block was printed at.
- A non-SGR CSI in a prose stream cannot change the collected style. Failure shows as text after
  an erase or cursor-move sequence coming back bold, faint or coloured.

## Execution Summary
Three defects in the prose replacement path (#R2WQ), all of the same shape as #HCR2: a block the
view re-wraps is a second rendering of content that is still in the grid, and each consumer of it
had to be taught that separately.

1. **The find counted re-wrapped matches twice.** `TerminalView::searchMatchCount()` and the count
   `searchStep()` reports added the core's total to `FoldSearch::matchCount()`, while the core goes
   on matching the real rows a taken-over block hides — `core.step` already steps past those, the
   counts did not. `VtCore::searchMatchesInRows(fromRow, toRow)` is new (default `-1` = "cannot
   say"), `LibVtermCore` answers it from its own match list, `FoldLayer::hiddenRowRanges()` names
   the hidden rows, and `TerminalView::hiddenCoreMatches()` subtracts them from both counts.
   libghostty-vt exposes only the total and the viewport's matches, so it answers `-1` and its
   count is left as it was.
2. **Strikethrough was dropped on takeover.** `FoldSpan`/`FoldLayer::Cell` carry `strike`,
   `ProseCollector` keeps SGR 9 (and 29) in its state and its `sgr` string,
   `CallLines::appendMarkdown` reads 9 too (and `sameInk` compares it), and both fold painters draw
   the rule in the cell's final ink, across blank cells, exactly as `paintRow` draws `AttrStrike`.
3. **`ProseCollector` treated every CSI as an SGR.** Only a final `m` reaches `applySgr()` now, the
   guard `CallLines::appendMarkdown` always had. Latent today — `Pane::sanitize` strips ESC from
   agent text — so this one is hardening, not an observed failure.

## Tests
`RELAY_ENGINE_TEST=ViewTest build/engine/relay-engine-tests findCountsRewrappedMatchesOnce`
`RELAY_ENGINE_TEST=ViewTest build/engine/relay-engine-tests strikethroughSurvivesRewrap`
`RELAY_ENGINE_TEST=FoldLayerTest build/engine/relay-engine-tests proseSpansKeepStrikethrough`
`RELAY_ENGINE_TEST=FoldLayerTest build/engine/relay-engine-tests proseSpansIgnoreNonSgrControlSequences`
`ctest --test-dir build -R '^(calllines|markdown)$'`

The find test pins the double count it replaces: at a taken-over width it asserts
`hiddenCoreMatches() == 2` beside a reported total of 2, so the pre-fix answer was 4. Whole classes
run clean under Xvfb: FoldLayerTest 34 passed, ViewTest 60 passed, calllines and markdown passed.
