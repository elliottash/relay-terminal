---
id: ESDF
type: work
status: needs-verification
assignee: agent
implemented_by: glm/glm-5.3
session: d863fae3-5226-479c-9caa-0d1a39081d0c
rank: zzzzzzzzzzzzzzzzw
created: '2026-09-21'
links: {commits: [dc5b457f6189602f1012e8cb13b764b16256492c, 3303f02a0951367fbb2ae31bf8f5f79e99df4beb, 914adee51e9574027089cb8ed6e812ceada73a15, 4b1f1d40c7323d3258347920e2e934f4507c6ced], evidence: [docs/qa_evidence/2026-09-24-esdf-flat-board/, docs/qa_evidence/2026-09-23-tryit-ESDF/], github: null, plans: [], related: ['#D9AQ']}
---
# switchboard needs an easy way to show me recent cards without the section orderi…

## Issue
switchboard needs an easy way to show me recent cards without the section ordering.

and i want to discuss -- whether the ordered sections are the best approach on the switchboard now.  i think it would be better if that was one of the sort options. instead the stage should be a column

## Plan
**Goal.** Give the Switchboard a flat, unsectioned view so recent cards are one click away, and make the stage a column of the row instead of only the list's grouping. Grouping-by-stage becomes one of the list's sort options, per the owner's direction in the Issue.

**Findings.**

- Today the list is always sectioned: `board::Model::sections()` and `Model::rows(collapsed, hidden, selfClosedOpen)` (`src/BoardModel.h/.cpp`) emit a `Row::Section` header per status column from `board.yaml`'s `columns:`, then extras (Verified, Done), then card rows.
- Sorting exists but only orders cards *inside* each section: `board::Sort` (`Manual, NewestFirst, OldestFirst, RecentlyUpdated, OldestUpdated, TitleAsc, TitleDesc, PriorityHigh, PriorityLow`), driven by the clickable `ColumnHeader` over the list (`⚑ · Card · Created · Updated`; `SortColumn`, `nextColumnSort`, `sortColumnIndex`, `syncColumnHeader` in `src/BoardPane.cpp`, click handler at `BoardPane.cpp:4019`). There is no flat view and no way to see "recent cards" across stages.
- The row already has fixed cells (flag, `#ID`, title, badges, Created, Updated); a card's stage is `board::statusTitle(card.status)` and multi-status sections already surface it as a `Badge::Status` badge (`badges(card, showStatus, …)`).
- The sort is persisted per pane in the layout node `{"board": {…, "sort"}}` via `BoardView::sortOrder()`/`setSortOrder()` (`src/BoardPane.h:248-253`, `BoardPane.cpp:6816`); folded/hidden sections and label chips persist the same way.
- `rank` is a global fractional index, so Manual order is meaningful across sections, not only inside one.
- Pure logic is widget-free and tested in `tests/boardmodel_test.cpp`; filter interplay in `tests/boardfilter_test.cpp`.

**Steps.**

1. `src/BoardModel.h/.cpp`: add `enum class Grouping { Sections, Flat }` with `groupingId`/`groupingFromId` (`"sections"`, `"flat"`; unknown reads as Sections, mirroring `sortFromId`), and `Model::setGrouping(Grouping)` / `grouping()`.
2. `Model::rows()`: add the Flat path — no `Row::Section` headers; one sequence of every shown (filter + label chips + hidden-section checkboxes still applied) card ordered by the current `board::Sort` across the whole list (Manual orders by global `rank`); every row gets `showStatus = true`; self-closed fold rows are not emitted in Flat (their cards are ordinary rows — folding is presentation tied to sections); the signals block above the list is unchanged.
3. `src/BoardPane.cpp/.h`: add a **Stage** cell to the row (fixed-width text cell holding `statusTitle(card.status)`, drawn in Flat mode; in Sections mode the section header already says it, so the cell stays empty/hidden there) and a **Stage** cell to `ColumnHeader` (`syncColumnHeader`). A click on the Stage header toggles Grouping between Sections and Flat — that is the "stage as a sort option" affordance; the header cell wears the accent when the list is grouped by stage, the way the active sort cell wears its arrow.
4. Persist grouping per pane beside the sort: `BoardView::grouping()` / `setGrouping(id)` riding the layout node as `{"board": {…, "sort", "grouping"}}`.
5. Entering Flat from a Manual sort switches the sort to `RecentlyUpdated` (the card's first ask is "show me recent cards"); returning to Sections restores `Manual`. Section checkboxes, label chips and the text filter keep composing in Flat; fold/unfold keys (Left/Right on headers) no-op; `Alt+Shift+←/→` status moves and the `m` menu stay the way a card's stage is changed in Flat (a drag in Flat reorders rank only, and only under Manual).
6. Tests in `tests/boardmodel_test.cpp`: Flat ordering under each `Sort`; filter + label chips + hidden sections composing in Flat; no Section/Fold rows emitted in Flat; `groupingId`/`groupingFromId` round-trip; Manual-in-Flat follows global rank. Adjust `tests/boardfilter_test.cpp` if it asserts section headers unconditionally.
7. Docs: update the Sorting paragraph of `docs/SWITCHBOARD-DESIGN.md` 4.6 with the Grouping mode and the Stage column, noting the owner's 2026-09-21 decision on this card.

**Risks / decisions for the owner.**

- **Default view.** The Issue reads as wanting Flat-with-Stage-column to be the default and sections to become opt-in. The plan implements both modes and persists the choice per pane; whether the *default for a new pane* flips to Flat + Recently updated is the owner's call (recommend: yes, matching the Issue's stated preference).
- In Flat there is no drag-onto-a-section gesture, so a stage change is keyboard/menu only — flag if that feels like a loss.
- Verified and Done lose their special headers in Flat; their rows keep their `✓ verifier` and status badges, so the information survives, but confirm that is enough.

**Verify.**

- `scripts/relay-build`, then `ctest --test-dir build -R 'boardmodel|boardfilter'`.
- Live under Xvfb with an isolated `XDG_CONFIG_HOME`: open the Switchboard, click the Stage header, confirm one flat list sorted by Recently updated with a Stage cell per row, checkboxes/filter still narrow it, the choice survives a window-layout save/restore, and clicking Stage again returns the sectioned board exactly as it was.

## Done means
- A new Switchboard pane (desktop) opens as one unsectioned list of every shown card, newest-updated first, with each row's stage in its own column. Failure: cards still come grouped under stage headers by default, or a row has no stage.
- Clicking the Stage column header switches to the sectioned board and back, the choice survives a layout save/restore, and filter/label chips/section checkboxes still narrow the flat list. Failure: the toggle does nothing, forgets itself on restart, or the flat list ignores a filter.
- The phone board (`app/board.js`) has the same flat "recent" view with a stage chip per row and a way back to sections. Failure: the phone only shows sectioned stages.

## Tests

- `scripts/relay-build` clean; land.py's verify build of the exact landed tree exits 0 (twice: `dc5b457`, `3303f02`).
- Desktop: `ctest -R board` on the landed tree — `board` (incl. the new `aFlatListIsEveryShownCardInOneOrder` and `theStageHeaderTogglesTheFlatListAndTheChoiceIsKept`), `boardsections`, `boardsignals`, `boardpane`, `boardfilter`, `boardwatch` all pass. `boardworkspace`/`boardremote`/`boardexecute` fail identically on a clean export of tip **without** this change — pre-existing, filed as #D9AQ.
- Phone: `PYTHONPATH=backend python3 -m unittest tests.test_board_view` — all 34 pass, including the new `test_recent_opens_as_one_list_newest_first_each_row_naming_its_stage` (flat order and chips, toggle to Stages, `localStorage` persistence, back to Recent).
- Docs: `docs/BOARD-DESIGN.md` §4.6 carries the Grouping paragraph (SWITCHBOARD-DESIGN.md is a moved stub).

## Execution Summary

Landed in three commits, each with the build gate on the exact tree:

- `dc5b457` — desktop. `board::Grouping` (`sections`/`flat`, flat the default for a pane with no saved choice) drives `Model::rows()`: one list of every shown card in the current sort across the whole of it, each row carrying its `stage` for the new Stage column (left of the dates; a narrow row falls back to the status badge). The ColumnHeader gains a **STAGE** cell that toggles the two groupings, wears the accent under sections, and re-tooltips; entering flat from Manual turns on Recently updated, returning to sections restores Manual. Checkboxes, label chips and the filter compose in flat; ←/→ folding no-ops there; Alt+Shift+↑↓ and a drag reorder across stages without changing status (a stage moves by Alt+Shift+←/→ or the card's menu). The choice persists per pane as `grouping` beside `sort` in the layout node, and `docs/BOARD-DESIGN.md` §4.6 documents it with the owner's 2026-09-21 decision.
- `3303f02` — phone. `app/board.js` opens as one list of every shown card, newest first, with the stage as a chip on each row; a Recent/Stages pill pair above the list swaps views and keeps the choice in `localStorage.relay-board-grouping` beside the folded sections.
- `914adee` — evidence: `docs/qa_evidence/2026-09-24-esdf-flat-board/report.md` plus the new test's own phone screenshots.

On the way, `boardworkspace`/`boardremote`/`boardexecute` were measured failing at clean tip with and without this work — filed as #D9AQ, not fixed here.

## Try it

**To start:** `cd docs/qa_evidence/2026-09-23-tryit-ESDF && RELAY_BIN=/tmp/esdf-tryit/build/relay ./stage.sh` (rerunnable, disposable, no network; `./stage.sh --stop` takes it down). The window opens on the staged board.

**Task:** You are looking at a day of this staged project's work. Which card was touched last, and what stage is it in? Then click STAGE, and say which of the two views you want the Board to open on.

**Question:** In the Recent view, could you tell at a glance which card was touched last and what stage it is in — and is Recent the right view for the Board to open on by default, or should it open grouped by stage?

The AI's mechanical pass (flat default, STAGE both ways, sort cells, restart) is in `captures/` beside this card's evidence; the pass record is `ai-pass.md`. What the result should be is sealed in `expected.md` until you have answered.
