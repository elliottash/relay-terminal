---
id: ESDF
type: work
status: executing
assignee: agent
session: d863fae3-5226-479c-9caa-0d1a39081d0c
rank: zzzzzzzzzzzzzzzzw
created: '2026-09-21'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
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
