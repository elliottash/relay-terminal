---
id: K48R
type: work
status: needs-qa-llm
labels: [bug]
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 subagent (Claude Code session), 2026-09-19
rank: zzzzzzz
created: '2026-09-19'
acceptance: a streaming thinking fold never shows more than 6 rows of reasoning (the tail being written), a finished one opened by hand never more than 18 followed by "… N more lines · open in pane", on any length of reasoning; a test feeds a several-thousand-line block and asserts both caps
source: 'issues/bug_intake.txt, 2026-09-19: "the thinking bubble height isnt capped, its filling up multiple terminal pages."'
links: {plans: [], commits: [ebebb08, 41d4355], evidence: [docs/qa_evidence/2026-09-19-thinking-fold-cap/], related: [T8CN, QT8C], github: null}
---
# The thinking fold's height is not capped

## Issue
the thinking bubble height isnt capped, its filling up multiple terminal pages.

## Cause

`Pane::thinkingFoldLines()` sets `options.maxLines = 400` (`src/Pane.h`), for the streaming tail and
for the settled fold alike. Four hundred rows is several screens. #T8CN recorded Warp's numbers —
a clipped view of 120 px while streaming, 360 px when done — and they never made it into the fold.

## Decisions

- **Owner, 2026-09-19: Warp's sizes.** About 6 rows while streaming, about 18 when done. A grid fold
  has no scroll view of its own, so the two caps are:
  - *streaming*: the **last** 6 rendered rows — the end is the part being written — under a muted
    "… N earlier lines" row once there is more;
  - *done, opened by hand* (click, the thinking-panel shortcut, or `agent/thinking_display=always`):
    the **first** 18 rows, then "… N more lines · open in pane".
- The caps count rendered rows at the pane's current width, after markdown and wrapping, not source
  lines: one long paragraph must not escape the cap by being a single line.
- "open in pane" keeps pointing at the whole text. Until the agent internals pane (#QT8C) lands
  that is the turn pane; the inbox also says that link "doesnt work" today, so reproducing and
  fixing the click is part of this card.

## Implemented

- **The two caps.** `Pane::thinkingFoldLines()` asks for `calllines::kThinkingStreamRows` (6, with
  `tail`) while the block streams and `kThinkingDoneRows` (18, from the top) for a settled fold, and
  passes `foldWrapCells()` — the grid width less `relay::kFoldIndent`, which is where the fold layer
  wraps — and `tail` to `foldForMarkdown()`, which wraps before it counts. The cap is then in the
  rows the view paints, and the cut is named on a muted row (`… N earlier lines` above a stream,
  `… N more lines · open in pane` below a settled fold).
- **The wrap is the engine's own**, not a second implementation: `relay::wrapFoldLines()` and
  `relay::foldClusterWidth()` now live beside `FoldLine` in `engine/TerminalBackend.h`, `FoldLayer`
  keeps the one width table (`clusterWidth` forwards to it) and `kFoldIndent` is the indent both
  sides wrap at. `engine/tests/FoldLayerTest.cpp::preWrappedLinesLayOutOneRowEach` lays pre-wrapped
  content out through the layer at four widths and asserts one row per line, same text as before.
- **"open in pane" doesnt work** — reproduced live (`docs/qa_evidence/2026-09-19-thinking-fold-cap/`,
  and the repro under the old build showed it too): the click path was sound and the turn pane *did*
  open; the reasoning was not in it. `Pane::requestTurn()` wrote the thinking into the pane's log and
  then asked the worker for the transcript, and the reply — a round trip later — began with
  `m_log->clear()`. `TurnTranscriptView` now holds the thinking and the transcript and redraws both,
  so either may arrive first, and the fold's 4 Hz flush pushes the growing block into an open pane
  (`Pane::pushThinkingToTurnPane`), so the pane follows the stream instead of freezing at what had
  arrived when it was opened.
- Tests: `tests/calllines_test.cpp` (`theThinkingCapsHoldOnAnyLengthOrShapeOfReasoning` feeds a
  4 000-line block and a 5 000-character paragraph at 40 and 100 columns and asserts both caps and
  the row widths; `aSettledThinkingFoldKeepsItsHead`; `wrappingForTheCapKeepsTheSpansAndTheirInk`),
  `tests/turntranscript_test.cpp::theReasoningSurvivesTheTranscriptReply`,
  `engine/tests/ViewTest.cpp::aPlainClickOnALinkInsideAFoldOpensIt` (the fold-link click path had a
  Ctrl+click test only), `engine/tests/FoldLayerTest.cpp::preWrappedLinesLayOutOneRowEach`.
- Docs: `docs/ENGINE.md` ("The reasoning fold"), `docs/ARCHITECTURE.md` §8.

## QA checklist

- [ ] A long reasoning stream: the fold stays 6 rows tall (plus the "earlier lines" row) throughout.
- [ ] Opened after done: 18 rows and the "more lines · open in pane" row.
- [ ] Narrow pane (40 columns), one 5,000-character paragraph: still capped.
- [ ] "open in pane" on a thinking fold opens the full text.
