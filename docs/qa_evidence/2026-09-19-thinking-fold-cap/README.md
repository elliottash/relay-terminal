# The thinking fold's height cap — implementer evidence (issue K48R, 2026-09-19)

`drive.sh` boots Relay under Xvfb with an isolated `HOME`, `XDG_CONFIG_HOME`,
`XDG_RUNTIME_DIR` and `TMPDIR` and `RELAY_KEYRING=off`, points the pane's provider at
`stub-provider.py` (loopback, `reasoning` deltas over SSE) and shoots each scene. OCR of
each pane body is in `implementer-notes.txt`; worker logs per scene under `logs-<scene>/`.
Derived from the T8CN harness (`../2026-09-19-thinking-fold/drive.sh`).

## What each shot shows

| Shot | Claim |
| --- | --- |
| `implementer-stream-1.png` | 4 s into a 240-line block: the fold is **6 rows**, under `… 51 earlier lines · open in pane` |
| `implementer-stream-2.png` | 10 s in, 100 lines later: still **6 rows**, the note now `… 143 earlier lines` — the fold never grows |
| `implementer-stream-3.png` | The turn ended: collapsed to `▸ ✦ thought for 19 s`, the answer under it |
| `implementer-done.png` | The same, settled |
| `implementer-reopened.png` | Alt+R on the settled fold: the **first 18 rendered rows** (the heading, a blank, steps 1–16), then `… 224 more lines · open in pane` and the `open in pane` link |
| `implementer-narrow-stream.png` | A 40-column pane, one 5,000-character paragraph with no newline in it: **6 rows** while it streams |
| `implementer-narrow-done.png` | The same paragraph settled: 18 rows and `… 107 more lines · open in pane` — 125 rendered rows cut to 18. A cap counted in source lines would have called this *one* line and shown the lot |
| `implementer-link-before.png` | A settled fold with its `open in pane` row |
| `implementer-link-opened.png` | That row clicked (its word box found by OCR, `implementer-notes.txt` records the pixel): the turn pane opens beside the terminal and the log starts with `Thinking` and the whole reasoning, above the turn's messages |

The `-head.png` crops are the window header at 300 %; the `-body.png` crops are what was OCR'd.

## What was wrong, and what changed

- `Pane::thinkingFoldLines()` asked for `maxLines = 400` in both modes — several screens — and
  the count was of *logical* lines, so one long paragraph was one line whatever it wrapped to.
  It now asks for `kThinkingStreamRows` (6, tail) while the block streams and `kThinkingDoneRows`
  (18, head) for a settled fold, and passes `wrapCells` so `foldForMarkdown` counts the rows the
  view will paint. The wrap is the fold layer's own (`relay::wrapFoldLines`,
  `engine/TerminalBackend.h`), asserted against `FoldLayer::layout()` in
  `engine/tests/FoldLayerTest.cpp::preWrappedLinesLayOutOneRowEach`.
- **"open in pane doesnt work"**: the link fired, the turn pane opened — and the reasoning was
  not in it. `Pane::requestTurn()` wrote the thinking into the log and *then* asked the worker for
  the transcript; the reply landed a round trip later and `setTranscript()` began with
  `m_log->clear()`. `TurnTranscriptView` now holds the thinking and redraws it with the log, so
  either may arrive first (`tests/turntranscript_test.cpp::theReasoningSurvivesTheTranscriptReply`),
  and the fold's 4 Hz flush pushes the growing block into an open pane so it follows the stream.
  The click path itself was sound and now has a plain-click test of its own
  (`engine/tests/ViewTest.cpp::aPlainClickOnALinkInsideAFoldOpensIt`); it only ever had a
  Ctrl+click one.

## Suites

Recorded in `implementer-suites.txt` beside this file: `./scripts/test.sh` (backend + Bash/PTY)
and `ctest --test-dir build`, both from the checkout this landed from.
