---
id: R2WQ
type: work
status: ready
labels: [change, bug]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
rank: b
created: '2026-09-19'
acceptance: 'An agent reply printed at one pane width still reads as wrapped prose after the pane is made narrower and wider again: no row ends mid-word, no stranded short rows from the old width, bullets still hang under their text. `relay-wordwrap-tests`, `relay-engine-tests` and `ctest` pass; QA confirms it live at three widths with both cores'
source: 'owner in chat, 2026-09-19, with two screenshots of the same reply wide and narrow: "check out this linre break / word wrap issue. if i shrink the pane, the lines break improperly. can you fix that?"'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-19-relays-own-lines-do-not-reflow/'], related: [TW84, SB7K, TK9C], github: null}
---
# Relay's own lines do not reflow when the pane is resized

## Issue

check out this linre break / word wrap issue. if i shrink the pane, the lines break improperly.
can you fix that?

Two screenshots of the same reply, in
[`docs/qa_evidence/2026-09-19-relays-own-lines-do-not-reflow/`](../../docs/qa_evidence/2026-09-19-relays-own-lines-do-not-reflow/):
`report-wide.png` (as printed) and `report-narrow.png` (the same rows after the pane was
shrunk), which shows both artifacts described below.

## Diagnosis

This is the other half of #TW84, and #TW84's "Limits" section predicted it in one sentence: "the
breaks are real newlines … resizing the pane later does not re-flow a reply already printed."
The owner has now hit it, so it is a bug, not a limit.

**What Relay does at print time.** Agent prose goes MarkdownAnsi → `relay::WordWrap` →
the terminal, in `Pane::wrapped()` (`src/Pane.h:11318`), which calls
`m_wrap.setColumns(m_backend->columns())` on every write. Where the wrapper decides to break,
it writes a **real `\n`** into the byte stream (`WordWrap::emitWord`, `src/WordWrap.cpp:92`) and
opens the next row with `CSI <indent> C` for the hanging indent. The pane's width at that instant
is therefore baked into the scrollback as line structure.

**What the emulator does on resize.** Both cores reflow their scrollback — `LibVtermCore`'s host
ring in `Private::rewrap()` (`engine/core/LibVtermCore.cpp:397`, called from `resize()` at 858),
libghostty-vt natively. Reflow rejoins and re-splits only **soft-wrap continuation runs**:
`rewrap()` walks forward while `lines[j].continuation` is set, and `Line::wrapColumns`
(`engine/core/CellTypes.h:71`) records the width a line was stored at. A hard `\n` starts a line
that is not a continuation of anything, and no terminal undoes it — that is the contract, and both
cores are right.

**So shrinking produces exactly the two artifacts in `report-narrow.png`:**

1. Every row Relay wrapped is its own logical line. One that is longer than the new width is
   soft-split by the core at the column where it runs out — mid-word:
   `RELAY_ENGINE_WITH_` / `GHOSTTY=ON`, `theRowRoleOscMarks` / `ItsLine(ghostty)`, `ghostt` / `y's`.
2. Rows that were already short stay short, stranded where the *old* width put them, still
   carrying the `CSI 2 C` hanging indent they were given then — `  pixel-level ViewTest row-role
   test green.` sits on a line of its own with two-thirds of the row empty beside it.

Widening has the mirror fault: nothing rejoins, so the reply keeps the narrow measure for good.

**Why it cannot be fixed inside the emulator.** A word-aware reflow in `LibVtermCore::rewrap()`
would fix the fallback core only; the default core reflows inside libghostty-vt, which is a pinned
upstream checkout, not a fork (`engine/scripts/build-libghostty-vt.sh`), so there is nothing to
patch. And a grid-wide word-aware reflow would be wrong anyway — it would re-flow `ls` columns and
program output that must keep their character alignment.

**A second, independent bug found while diagnosing this.** The fold layer's own wrap is
character-based too: `FoldLayer::layout()` (`engine/view/FoldLayer.cpp:53`) and
`relay::wrapFoldLines()` (`engine/TerminalBackend.h:101`) fill each row cluster by cluster until
the next one does not fit. Expanded tool-call detail therefore breaks mid-word at *any* width, not
only after a resize. One shared break rule fixes both.

## The fix

Relay's own prose stops depending on the width it was printed at: the printed rows stay in the
grid exactly as they are today, and the **view re-wraps them itself whenever the pane is not at
that width**. This is the shape the fold layer already has — host-owned text, laid out into
grapheme cells, wrapped with a hanging indent, re-laid-out on `setGeometry()`, painted as virtual
rows interleaved with real ones, anchors re-resolved after every resize and trim
(`engine/view/FoldLayer.h`). It is also Warp's shape: a reply is an object the view lays out, not
cells frozen at one width.

Nothing changes until the pane is resized. At `columns == printColumns` the layer stands aside
entirely: same pixels, same rows, same copy, same cost.

- [ ] **A reflow-proof anchor for each block.** Wrap every inline block Relay prints in an OSC 8
      run `relay://prose/<pane>/<block>`. Per-cell hyperlink ids survive scrollback and reflow in
      both cores (libvterm patch 4, `engine/third_party/libvterm/README.relay.md`; ghostty
      natively), and `FoldLayer` already resolves anchor runs to absolute rows from
      `VtCore::hyperlinkRuns()` and re-resolves them after resize, trimming and clearing — that is
      exactly the lookup needed, and absolute row numbers cannot be used instead because reflow
      changes them. `relay://prose/` must be **non-interactive** in `TerminalView`: no hover
      underline, no link cursor, no Ctrl+click, and not handed to the URL handler
      (`src/WindowManagerImpl.h:263` already keeps `relay://call/` away from it). <!-- t:a1 -->
- [ ] **Hand the view the block's logical lines.** At print time the pane gives the backend the
      block's text *before* `WordWrap` — as `FoldLine` spans, so bold/italic/links/colour survive —
      together with `printColumns`, the width it was printed at. `Pane::wrapped()` is the one place
      that sees both the unwrapped `rendered` string and the width, so this is one call at an
      existing seam. Keep printing through `WordWrap` as now, so a pane that is never resized is
      byte-identical and the scrollback text, its copy path and the restart snapshot (#SB7K) are
      untouched. <!-- t:a2 -->
- [ ] **Replacement blocks in `FoldLayer`.** Today a fold only *inserts* rows. Add: a fold may
      **replace** the real rows of its anchor run. `Fold` gains the replacement flag and the hidden
      real-row span; `visualTotal()` subtracts the hidden rows; `at()` maps a visual row inside the
      span to one of the block's wrapped rows; `visualOfReal()` on a hidden row answers the block's
      first visual row. Take-over is decided per block: hide only while
      `m_columns != f.printColumns`. Selection, copy and search over those rows take the fold path
      that already exists (`cellsText()`, `FoldSearch`), so no new copy or search code is needed —
      only the tests that prove it. The maths is headless: extend
      `engine/tests/FoldLayerTest.cpp`. <!-- t:a3 -->
- [ ] **One word-aware break rule, used everywhere.** Break before a word that would cross the
      edge; a continuation row takes the line's hanging indent (past a list/quote marker, else the
      leading spaces, dropped when it would be past half the row); a word wider than a row is left
      to break at the edge. Those are already `relay::WordWrap`'s rules
      (`src/WordWrap.cpp`, `isMarker()`, `endOfFirstWord()`, `emitWord()`) and they must not be
      written a second time — factor out "given cell widths and break opportunities, where do the
      rows end" and call it from `WordWrap`, `FoldLayer::layout()` and `relay::wrapFoldLines()`.
      `relay-markdown` is Qt::Core-only with `PUBLIC src` includes, so
      `relay-terminal-engine` can link it with no cycle. Tool-call detail stops breaking mid-word
      as a side effect. <!-- t:a4 -->
- [ ] **Tests.** `tests/wordwrap_test.cpp`: the shared rule, unchanged behaviour at the print
      width. `engine/tests/FoldLayerTest.cpp`: word breaks, hanging indent, hidden-row maths,
      visual↔real mapping with a replacement block, and the no-op at the print width.
      `engine/tests/ViewTest.cpp`: a pixel-level test that prints a paragraph and a bulleted list
      at 80 columns, resizes to 48 and back to 80, and asserts the rows are the ones the wrapper
      would have printed at each width — the regression this card exists for. <!-- t:a5 -->

## Boundary

Program output — the shell, Claude Code, vim, anything Relay did not print itself — keeps the
terminal's character reflow, which is correct and must not change. A block whose anchor has been
trimmed out of the scrollback, and a scrollback restored from the #SB7K text snapshot after a
restart, have no anchor and no logical lines: they stay as they are, frozen at the width they were
printed at, exactly as today.

## QA checklist

- [ ] Ask for a long paragraph, a bulleted list and a line with inline code and a link. Shrink the
      pane by half: no row ends mid-word, no row is stranded short, bullets still hang.
- [ ] Widen it past the original width: the text re-flows to the wider measure.
- [ ] Drag the splitter continuously: the text keeps up and nothing is lost or duplicated.
- [ ] Select and copy across a re-wrapped reply; search for a word that a re-wrap moved to another
      row.
- [ ] A user-typed `✦` line keeps its role band across the resize; the theme still recolours it.
- [ ] Expanded tool-call detail no longer breaks mid-word.
- [ ] Shell output above the reply (`ls`, `git log`) reflows the way it always did.
- [ ] Both cores: libvterm and `RELAY_ENGINE_WITH_GHOSTTY=ON`.
