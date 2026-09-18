---
id: T7BQ
type: work
status: needs-qa-llm
labels: [feature]
component: [gui, theme]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-18
rank: zzzzk
created: '2026-09-18'
acceptance: 'A checkbox per section at the top of the board''s list page, all ticked by default; unticking one takes that section off the list, the counts say so, and the set survives a restart'
source: 'owner, 2026-09-18: "at the top of the base page, there should be filter checkboxes at the top for the different sections."'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-board-list-page-tools/'], related: [EVW1], github: null}
---
# Section checkboxes at the top of the board, and room for "Clean up"

## Request
at the top of the base page, there should be filter checkboxes at the top for the different
sections.

## Change

Under the filter row on the board's list page (`#EVW1` put that row there), one checkbox per
section, ticked by default.

- **The sections come from the model**, not from a list in the code: `Model::sections()` — the
  columns `issues/board.yaml` configures, in its order, then a section for any status those do not
  collect (a plan's Draft, a Deferred card), then Done. A board with a different `columns:` gets
  different boxes, and a status that appears on a card gets its box the moment it does.
- **Unticked takes the section off the page** — header, cards and count. That is a different thing
  from folding it, which leaves the header with its cards put away, and the two are stored apart:
  `BoardView::hiddenSections()` beside `collapsedSections()`.
- **It composes with the text filter** rather than being overridden by it. A search that matches a
  card in an unticked section still does not put the section back.
- **The counts stay honest.** `Model::hiddenCount()` counts the open cards the boxes are keeping
  out, so the label reads `62 of 84 open` rather than pretending the board shrank; its tooltip
  says how many are hidden, and each box's tooltip says how many cards its section holds. With a
  text filter set the label is still "N shown", which counts exactly the rows on screen.
- **They wrap.** A pane can be ~350 px wide and there are eight or nine of them, so the row is a
  small flow layout (Qt ships none) that breaks onto further lines. The labels are uppercase mono,
  the same engraved treatment as the section headers they switch (SWITCHBOARD-AESTHETIC 3.1).
- **"Clean up"** (`#boardCleanup`) takes its place in the same row: "Have the agent tidy the
  board: merge or split sections and cards, review statuses". The backend message for it does not
  exist yet, so `BoardView::requestCleanup()` — last function in `src/BoardPane.cpp`, so wiring it
  is one isolated edit — says "Board cleanup is not wired yet." on the window's status bar *and*
  on the board's own notice, because a window with no status bar showing would otherwise make the
  button look broken rather than unfinished.

## QA checklist

1. **One box per section**, in the list's own order, every one ticked, the first time the board is
   opened. The names match the section headers below.
2. **Untick one.** Its header and all its cards leave the list at once, and the count beside the
   filter changes from `84 open` to `62 of 84 open` (the second number does not move).
3. **Tick it again.** Everything comes back, and the count goes back to `84 open`.
4. **With a text filter set** (`label:bug`, or a word), an unticked section stays off the list even
   when a card in it matches. The count says "N shown" and N is the number of card rows on screen.
5. **Untick every box.** The list says "Every section is hidden. Tick one at the top to see its
   cards." rather than "Nothing open", which would be a lie.
6. **Narrow the pane to ~350–420 px.** The boxes wrap onto further lines; none is clipped, and the
   list below is not pushed off the bottom. **+ New card** and **Clean up** drop to a line of
   their own with their labels whole (the pane's hover buttons keep their room in the top row
   whatever happens, so at that width there is none left beside the filter).
7. **Folding is still folding.** Left/Right on a row, and a click on a section header, still fold
   and unfold; a folded section's header is still there with its count. Folding and unticking do
   not disturb each other.
8. **Restart.** Untick a box, quit, start again: the unticked set comes back with the window's
   layout, the way the folded set does. *(Not yet seen live — see Known gaps.)*
9. **"Clean up"** shows its tooltip and, for now, says "Board cleanup is not wired yet." on the
   board and in the status bar. Its label is not clipped at any pane width. (It is a QToolButton,
   which `tests/buttonfit_test.cpp` does not walk — that test is about QPushButton's `:default`
   font trap — so this one is checked by eye, in shots 01 and 06.)

## Known gaps

- **Persistence is wired but not yet seen live.** The coordinating session added the three lines
  in `src/main.cpp` (`ToolPane::node()` writes `"hidden"`, the layout reader passes it to
  `createBoardPane`, which calls `setHiddenSections`), mirroring the folded set. It builds and
  `ctest` passes; nobody has yet quit and restarted with a box unticked, so checklist item 8 is
  QA's to confirm.
- The boxes have no keyboard path of their own; `/` and the filter language (`status:`) remain the
  keyboard way to slice the list, so no shortcut hint was added for them.
