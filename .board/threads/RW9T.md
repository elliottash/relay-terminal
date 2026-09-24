<!-- relay:entry 20260922T030700Z-a1 author=claude-code kind=progress -->
### Claude Code · 2026-09-22 03:07
Filed and claimed. Found by auditing the prose replacement path after #HCR2:

1. `TerminalView::searchMatchCount()` adds the core's count to `FoldSearch::matchCount()`, but
   the core also matches the real rows a taken-over replacement fold hides — `searchStep` skips
   those deliberately (`TerminalView.cpp`, `core.step`), the count does not. Every match inside a
   re-wrapped reply is counted twice.
2. `FoldSpan` has no strike flag, so `ProseCollector` drops SGR 9 and a `~~struck~~` run loses its
   rule as soon as the block is taken over (the grid paints it: `AttrStrike`).
3. `ProseCollector` applies SGR semantics to every CSI final byte, not only `m` — latent today
   because `Pane::sanitize` strips ESC from agent text, but its sibling parser in `CallLines.cpp`
   guards it and this one does not.

Fixing all three on this card.

<!-- relay:entry 20260922T033000Z-b2 author=claude-code kind=evidence -->
### Claude Code · 2026-09-22 03:30
Landed 8164a9e7 through the build gate (the exact tree builds `relay`). All three fixed:
the find subtracts the core's matches on hidden rows, strikethrough is carried through
`FoldSpan`/`Cell` into both fold painters, and only a final `m` reaches `applySgr()`.

Tests, under Xvfb with an isolated XDG_CONFIG_HOME and `RELAY_KEYRING=off`:
FoldLayerTest 34 passed (including `proseSpansKeepStrikethrough`,
`proseSpansIgnoreNonSgrControlSequences`), ViewTest 60 passed (including
`findCountsRewrappedMatchesOnce`, `strikethroughSurvivesRewrap`), `ctest -R '^(calllines|markdown)$'`
passed. The find test asserts `hiddenCoreMatches() == 2` at a taken-over width beside a reported
total of 2, which is the double count it replaces.

Not done, and why: `AttrConceal` and the double/curly underline distinction still have no
representation in a fold cell. No prose writer emits either — MarkdownAnsi emits 1, 2, 3, 4, 9 and
the ink — so plumbing them would be fields nothing sets. If a writer ever does, it belongs with
this card's shape. Card moved to done.
