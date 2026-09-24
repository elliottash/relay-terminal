---
id: 8SBD
type: work
status: needs-verification
assignee: claude-code
rank: zzzzzzzzzzzzzzzz
created: '2026-09-20'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-block-rows-selection/README.md], related: [], github: null}
---
# bug: when you copy from pane , line breaks are dropped rather than replaced with…

## Issue
bug: when you copy from pane , line breaks are dropped rather than replaced with a space

so you get words concatenated together

## Plan
**Goal.** Copying a multi-line selection from a terminal pane must never glue two words together: where a soft-wrapped line broke at a space, the copied text keeps that space (character-exact join), and hard line breaks keep coming out as `\n`.

**Findings.**

- The bug is in the libvterm engine core's selection text. `LibVtermCore::selectedText()` (`engine/core/LibVtermCore.cpp`, ~line 1430) joins a row followed by a continuation (soft-wrapped) row with **nothing** — correct in principle — but it takes each row through `Line::text(from, to)` (`engine/core/CellTypes.cpp`, ~line 54), which **trims trailing spaces** (`while (cps.back() == U' ') pop`). A line that wrapped exactly at a printed space ("foo " / "bar", the common case when prose wraps at a word boundary) loses the space and copies as "foobar". This is the reported "words concatenated together".
- The same trim-at-wrap risk exists in two more places: `GhosttyCore::selectedText()` (`engine/core/GhosttyCore.cpp`, ~line 1225) delegates to libghostty-vt's `selection_format` with `unwrap=true, trim=true` (`formatSelection`, ~line 218) — whether it drops the space at a wrap point needs the new test to say; and `TerminalView::visualSelectedText()` (`engine/view/TerminalView.cpp`, ~line 2500) joins a fold's wrapped rows via `FoldLayer::cellsText` — check it for the same trim.
- Hard (non-continuation) breaks already copy as `\n` in the core, and the copy-on-select helper for read-only Qt surfaces converts U+2029 to `\n` (`src/CopyOnSelect.h`), so no change is needed there. `Line::text()`'s trimming is wanted by its other callers (`screenText()`, `historyText()`, painting helpers) — do not change its default.

**Steps.**

1. Add a failing test in `engine/tests/CoreTest.cpp` (it already has `selectedText()` cases at ~line 496): on a narrow grid, print a line that soft-wraps exactly at a space, select across the wrap, and expect the space kept ("foo bar", not "foobar"). Also keep a case where the wrap falls mid-word (a long token/URL) — that must join with nothing, so wrapped URLs still copy intact.
2. Fix `LibVtermCore::selectedText()`: when the next row is a continuation, take the row's text **without** the trailing-space trim (add a trim flag or a small `untrimmedText(from, to)` on `Line` in `engine/core/CellTypes.{h,cpp}`), joining with nothing. Keep trimming on the final row of the selection.
3. Run the same test against the ghostty core if this build has it; if it fails the same way, fix `formatSelection`'s options (e.g. `trim=false` and trim in Relay code) so both cores behave identically.
4. Check `TerminalView::visualSelectedText()` / `engine/view/FoldLayer.cpp`'s `cellsText` for the same trim at a fold row's wrap point; fix to match step 2 if present.
5. Build with `scripts/relay-build` and verify live under Xvfb: print a long prose line, select across the wrap, paste into an editor — words separated, URLs whole.

**Risks.**

- Rule choice: the recommendation is character-exact join (keep whatever was printed at the wrap, usually the space; nothing when the wrap is mid-token), which is xterm/Konsole behaviour and keeps URLs intact. The issue's "replaced with a space" wording could also read as "always insert a space at a wrap" — that would corrupt wrapped paths/URLs, so it is not recommended; flag to the owner only if they push back.
- Step 3 may be blocked if this machine cannot build the ghostty core; if so, land the libvterm fix and note ghostty as untested.

**Verify.**

- New `CoreTest` cases pass: `ctest --test-dir build -R core` (or the engine test target that runs `engine/tests/CoreTest.cpp`).
- Live check under Xvfb: copy across a soft wrap and across a hard break; pasted text has a space at the wrap and `\n` at the break.

## Execution Summary
The plan's five steps, in two commits, because copying reads two different things depending on
the pane's width.

**The core (steps 1–3), `4e622af8`.** `Line::untrimmedText(from, to)` reads a row exactly as
printed, without `text()`'s trailing-space trim, while still dropping columns the terminal never
wrote to — a row that wrapped because a double-width character would not fit ends in a blank
nobody typed, and making that a space would break the wrapped word the other way.
`LibVtermCore::selectedText()` decides whether the next row is a continuation *before* taking the
row's text and uses the untrimmed read for a row that is continued. `Line::text()` is byte-for-byte
unchanged, so `screenText()`, `historyText()` and the painting helpers keep their trim. Step 3 is
not runnable here: this build has `RELAY_ENGINE_WITH_GHOSTTY=OFF` and no prebuilt `libghostty-vt`,
so `GhosttyCore.cpp` was left alone rather than edited blind — the new cases are written over
`availableVtCores()`, so a build that has the ghostty core runs them against it unchanged and will
say whether `formatSelection`'s `trim=true` drops the space.

**The view (step 4), `4a4663e0`.** Away from the width it printed at, a block is re-wrapped by
FoldLayer and the grid rows are hidden (#R2WQ), so the copy goes through
`TerminalView::visualSelectedText()` instead of the core. That path had the same fault in its own
form: the space a line wraps at belongs to neither row, and the join only concatenated the rows'
displayed cells. It now carries the cells between one row's end and the next row's first. A wrap
inside a token leaves no such cell and still joins with nothing.

The rule is character-exact in both, as the plan's Risks section recommended: whatever was printed
at the wrap separates the two words, and nothing is inserted mid-token, so a wrapped URL or path
copies whole.

## Tests
- `CoreTest::selectionKeepsThePrintedSpaceAtASoftWrap` — a 10-column grid where `the quick brown`
  wraps after the space, selected across the wrap, across a hard break, and starting mid-row.
  Verified to catch the bug: with the fix reverted it fails with `"the quickbrown"`.
- `CoreTest::selectionAcrossAMidTokenWrapJoinsWithNothing` — a URL wrapping twice mid-token copies
  whole.
- `ViewTest::copyingAcrossAWrappedEdgeKeepsItsSpace` and
  `ViewTest::copyingAcrossAWrappedTokenInsertsNothing` — the same two rules on a re-wrapped block.
- On the landed tree: `ViewTest` 56/56, `CoreTest` 45/45, `FoldLayerTest` 32/32, and the audit
  probe 6/6 both on a block's own rows and on the grid rows.
  Evidence: `docs/qa_evidence/2026-09-21-block-rows-selection/`.

## QA checklist
- [ ] In a pane, print a long prose line that soft-wraps at a space (`seq 1 40 | paste -sd' '`, or
      any wide agent reply), select across the wrap and paste into an editor: the two words are
      separated by a space, not glued together.
- [ ] Do the same across a **hard** line break: the paste has a real newline.
- [ ] Select across a wrapped URL or long path: it pastes whole, with no space inserted inside it.
- [ ] Repeat all three after narrowing the pane so an agent's reply re-wraps — that is the other
      code path, and both must agree.
- [ ] Ghostty core: unverified here (not buildable on this machine). If a build with it exists, run
      `RELAY_ENGINE_TEST=CoreTest relay-engine-tests selectionKeepsThePrintedSpaceAtASoftWrap`
      there; the case runs against every available core as written.
