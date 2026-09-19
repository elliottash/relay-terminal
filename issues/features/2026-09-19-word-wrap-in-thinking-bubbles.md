---
id: EH98
type: work
status: inbox
rank: zzzzzzzzzi
created: '2026-09-19'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# word wrap in thinking bubbles

## Issue
text is not wrapping at word boundaries in the agebt thinking bubble. that should be fixed to make it more readable.

## Plan
**Goal.** The agent's reasoning, shown in the terminal's `▸ ✦ thinking…` fold, breaks between words at the pane's width — as every other prose surface in Relay already does — instead of mid-word at the exact column.

**Findings.**

- The desktop's thinking surface is a fold in the terminal (since T8CN; the old floating overlay is gone). `Pane::thinkingFoldLines` (`src/Pane.h:4345–4466`) builds its rows with `relay::calllines::foldForMarkdown(text, palette, options, foldWrapCells(), tail)` (`src/CallLines.cpp:566`), which pre-wraps them with `relay::wrapFoldLines()` (`engine/TerminalBackend.h`, inline, ~line 101); the view wraps again the same way in `FoldLayer::layout()` (`engine/view/FoldLayer.cpp`).
- Both walk the line **grapheme cluster by cluster and break only when the next cluster does not fit** — there is no notion of a word boundary, so prose splits mid-word at the right edge. That is the fault the card names.
- Everywhere else already wraps at words: the pane's own prose goes through `relay::WordWrap` (`src/WordWrap.cpp`; `src/Pane.h:11367` — "a line breaks between words at the pane's width rather than wherever the terminal runs out of columns"), the phone bubble `​.rp-thinking-tail` (`app/pane.css:118`, `pre-wrap` + `overflow-wrap`) and the Qt remote bubble `m_thinkingTail` (`src/RemotePane.cpp:903`, QPlainTextEdit's default `WrapAtWordBoundaryOrAnywhere`). No change is needed on those surfaces; the fold is the one left.
- The fold's caps count rendered rows after wrapping (`kThinkingStreamRows` 6 / `kThinkingDoneRows` 18, `src/CallLines.h:43–45`), so word-aware rows need no new machinery there.

**Steps.**

1. In `engine/TerminalBackend.h`, put the row-break decision in one shared helper beside `wrapFoldLines` (it takes a line's cluster widths plus an is-space flag per cluster and returns `(first, count)` row boundaries): a row breaks **after the last space that keeps the next cluster within `usable`**, falls back to today's cell-limit break when the row holds no space (a word wider than the row still breaks anywhere), and never opens with a space — the space stays at the end of the first row so `lineText()` and the copy path round-trip unchanged. Spaces are U+0020 and tab, in both callers.
2. Rewrite `relay::wrapFoldLines()` in that header to use the helper, and make `FoldLayer::layout()` (`engine/view/FoldLayer.cpp`) drive its `Cell` rows from the same helper — the two cannot then drift, which `engine/tests/FoldLayerTest.cpp::preWrappedLinesLayOutOneRowEach` asserts.
3. Tests. `engine/tests/FoldLayerTest.cpp`: a prose line wider than `usable` breaks at a space (`rowText` rows end at word boundaries); an overlong word still breaks mid-word; extend `preWrappedLinesLayOutOneRowEach`'s source list with a space-containing line so the wrap↔layout agreement is asserted over word breaks. `tests/calllines_test.cpp`: extend `wrappingForTheCapKeepsTheSpansAndTheirInk` to assert no row ends mid-word where a space was available, and that joining the wrapped rows still gives the logical line (spaces intact).
4. Update the words that call this a hard wrap: the `wrapFoldLines` comment in `engine/TerminalBackend.h`, the header comment in `engine/view/FoldLayer.cpp`, and `docs/ENGINE.md` "Folds → the thinking fold" (~lines 305–309, "the same hard wrap at `columns - kFoldIndent`").
5. Build with `scripts/relay-build`; land per `scripts/land.py` (WARP.md).

**Risks.**

- The change is uniform over all fold content (tool-call detail, diffs, tasks), not thinking only — the layer cannot tell which fold it holds, and keeping wrap↔layout agreement requires one rule. A long diff row now may break a few cells earlier at a space; such rows already wrap today, so alignment was already broken. Acceptable, but it is a visible change beyond the thinking fold.
- Wrapped row counts shift for prose (word wrap wastes each row's tail), so the "… N more/earlier lines" counts change value; the 6/18 caps still apply after wrapping.
- Reading the card's "bubble" literally would point at the phone or remote bubble, but no wrapping fault exists on either (they wrap at words by construction); the terminal fold is the only surface that structurally breaks mid-word, so this plan fixes it. If you meant a different surface, say so before Execute.

**Verify.**

- `./scripts/test.sh` and `ctest --test-dir build` — the new and extended cases above, plus the existing invariants (`contentExpandsAndWrapsToTheWidth`'s space-free line is unchanged; cap tests' `row.size() <= cells` holds since rows only get shorter).
- Live under Xvfb with an isolated `XDG_CONFIG_HOME` (WARP.md): stream a turn's reasoning in a narrow pane — the `▸ ✦ thinking…` fold's rows break between words, and resizing narrower/wider re-wraps at word boundaries.
