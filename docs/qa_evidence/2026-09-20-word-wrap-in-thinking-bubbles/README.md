# #EH98 — word wrap in thinking bubbles: implementer evidence

**Date:** 2026-09-20 · **Executing session:** Relay agent pane, `kimi/kimi-k3` (session
`a8f74779e03a482fb8fd602409ab9aaa`) · **Tree:** `15aafa59d28d9259fc1f4b3aaff23362e6aa5eb5`
(the tip `scripts/land.py begin` snapped).

This is implementer evidence, not a QA verdict. A live-run check of the streaming fold is on the
card's QA checklist; nothing here claims it.

## What the card asked

The agent's reasoning fold (`▸ ✦ thinking…`) broke prose mid-word at the exact column instead of
at word boundaries.

## Finding at Execute time: the fix was already landed by #R2WQ

The card was planned 2026-09-19 21:13Z against a tree where `relay::wrapFoldLines()`
(`engine/TerminalBackend.h`) and `FoldLayer::layout()` (`engine/view/FoldLayer.cpp`) filled rows
grapheme cluster by grapheme cluster. About ninety minutes later (18:47 -0400), **#R2WQ**
("Relay's own lines do not reflow when the pane is resized", `implemented_by glm/glm-5.3`) landed
`121fd9a1` + `b6521ebc`, whose task a4 reads "One word-aware break rule, used everywhere …
Tool-call detail stops breaking mid-word as a side effect", and whose diagnosis names this card's
fault verbatim as "a second, independent bug found while diagnosing this". So when this card was
handed to a terminal pane (2026-09-20 02:50Z), the fix it plans was already in the tree.

**#EH98 therefore needed no code delta.** Verified at the tip above:

- `relay::wrap::rows()` (`src/WordWrap.cpp:76`) is the single break rule: a row ends before a word
  that would cross the edge; a word wider than a row breaks at the edge, where the terminal would
  break it; a space that would cross the edge is dropped and opens no row.
- `relay::wrapFoldLines()` (`engine/TerminalBackend.h:123`) — the pre-wrap the thinking fold's
  6/18-row caps count on (`src/CallLines.cpp:590`, from `Pane::thinkingFoldLines` →
  `calllines::foldForMarkdown(..., foldWrapCells(), tail)`) — builds its rows from that rule.
- `FoldLayer::layout()` (`engine/view/FoldLayer.cpp:148-165`) drives its `Cell` rows from the same
  rule, so host pre-wrap and view layout cannot drift
  (`FoldLayerTest::preWrappedLinesLayOutOneRowEach` pins the agreement).
- The words are already right: the `wrapFoldLines` comment, the `FoldLayer::layout()` header
  comment, and `docs/ENGINE.md` ("## Folds", the "Where a row may end…" passage and the
  thinking-fold passage ~line 336) all describe the shared word-aware rule. No "hard wrap"
  wording remains.

**Deviation from the card's plan, for the record.** The plan (written against the older tree)
said the space stays at the end of the first row so the wrapped rows join back to the logical
line. The landed rule instead *drops* an edge space (`wrap::rows`, the "An edge space opens no
row" branch), so joining painted rows loses the dropped space; `FoldLayer::lineText()` and the
copy path still return the logical line whole (`docs/ENGINE.md`: "a wrapped fold line comes back
as its one logical line, without the indent"). The owner-visible behaviour the card wants — rows
break between words — is the same.

## Verification run (this session; no new tests — owner 2026-09-20: "no tests, just deliver")

`scripts/relay-build`: clean, built in 46 s.

```
$ ctest --test-dir build -R "^(wordwrap|calllines|markdown|relay-engine-tests)$" --output-on-failure
1/4 Test #30: markdown ...........   Passed    0.01 sec
2/4 Test #31: wordwrap ...........   Passed    0.00 sec
3/4 Test #60: calllines ..........   Passed    0.11 sec
4/4 Test #63: relay-engine-tests .   Passed   11.68 sec
100% tests passed, 0 tests failed out of 4
```

Existing coverage those green runs include:

- `FoldLayerTest::foldRowsBreakBetweenWords` (added by #R2WQ): a prose line wider than the usable
  width breaks at spaces, every row ≤ usable, no row ends inside a word.
- `FoldLayerTest::preWrappedLinesLayOutOneRowEach`: `wrapFoldLines` and the layer agree row for
  row at 8/20/41/80 columns.
- `FoldLayerTest::contentExpandsAndWrapsToTheWidth`: a space-free 20-cell line still breaks
  mid-word at the edge — the overlong-word fallback the plan requires.
- `CallLinesTests::theThinkingCapsHoldOnAnyLengthOrShapeOfReasoning`: the 6/18 thinking caps hold
  at 40 and 100 columns with every wrapped row ≤ cells, for a several-thousand-line block and a
  single 5,000-character paragraph alike.
- `CallLinesTests::wrappingForTheCapKeepsTheSpansAndTheirInk`: spans, ink and links survive the
  wrap.

## Left to QA (on the card's checklist)

- Live Xvfb check of a streaming `▸ ✦ thinking…` fold at several widths (the card's Verify row).
- The ghostty core: `RELAY_ENGINE_WITH_GHOSTTY=ON` is off in this build dir — the same open
  both-cores row #R2WQ recorded.
- QA independence: the code under check is `glm/glm-5.3`'s (#R2WQ); this executing session is
  `kimi/kimi-k3`. Prefer a verifier outside both families (`scripts/relay-board.py verifier EH98`).
