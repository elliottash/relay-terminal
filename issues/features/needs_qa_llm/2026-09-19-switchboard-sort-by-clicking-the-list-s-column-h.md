---
id: Z2VT
type: work
status: needs-qa-llm
labels: [feature, switchboard, gui]
implemented_by: deepseek/deepseek-v4.1-flash
rank: zzzzzzzzi
created: '2026-09-19'
source: pane relay-terminal, 2026-09-19
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-switchboard-column-header/], related: [], github: null}
---
# Switchboard: sort by clicking the list's column headers, and reorder the sections in the gear

## Issue
change switchboard sorting from a sort button to adding header columns that you click on

add a "created" and "updated" column

and sorting is within section.

we do need sorting of sections though. enable those to be dragged and dropped, with up and down buttons for moving them, in the section settings modal

## Plan
**Goal.** The Switchboard's sort is the list's own column header — Card, Created, Updated, each a
click that orders the cards *inside every section* — and the section list's order is set in the
ing gear by drag and drop and by ▲ ▼ buttons.

**Where.**

- `src/BoardModel.{h,cpp}`: `Sort` gains `OldestUpdated`, `TitleAsc`, `TitleDesc` (ids
  `updated-oldest`, `title`, `title-desc`); the comparator compares by column with the board's own
  rank breaking ties; the pure column helpers `SortColumn`, `columnTitle`, `nextColumnSort`,
  `sortColumnIndex`, `sortAscending`, `dateCell`.
- `src/BoardPane.{h,cpp}`: the `Sort:` button and its menu are gone; `ColumnHeader` (three cells
  over the list, measured with the row delegate's own numbers so a label sits over its cells) is
  built with the list; the card row gains the two date cells and the fit rule that drops them with
  their labels in a narrow pane; `syncSortButton` → `syncColumnHeader`; the refused-reorder notice
  points at the header.
- `src/Theme.cpp`: the header cells' colours (muted, hover, accent when the sort is on).
- `src/BoardSections.{h,cpp}`: `SectionPlan::canMove` / `whyNotMove` / `move` / `moveBefore` and
  `syncColumns` (columns: in the drawn order); `SectionRow` (the drag source and the drop target,
  with `dropHere` as its entry point because Qt only routes a drop through its own drag machinery)
  and the per-row handle and ▲ ▼ buttons.
- `docs/SWITCHBOARD-DESIGN.md` 4.6 (the row, the sorting, the sections' order),
  `docs/SWITCHBOARD-FORMAT.md` (the gear's verbs).

**Notes.** A column sort still takes the manual reorder off, and a click cycles that column's two
orders and then back to Manual, so the board's drag order is always one click away. Verified and
Done stay the last two sections.

## QA checklist
Implementer evidence: `docs/qa_evidence/2026-09-19-switchboard-column-header/` (README,
`ocr.txt` receipts, the screenshots and `drive.sh`).

- [ ] The header sorts: each of the three cells orders the cards inside every section, a second
  click turns it round, a third gives the board its own order back, the active cell wears the
  accent and the arrow, and no `Sort:` button is left anywhere
  (`theColumnHeaderSortsTheListWithinASection`; live: `02` → `05`, the header row and arrow in
  `ocr.txt`).
- [ ] Sorting is within a section: one section's order never depends on another's
  (the test puts a second card in Inbox and reads Ready's order).
- [ ] The ids round-trip and an unknown one reads as Manual: `manual`, `newest`, `oldest`,
  `updated`, `updated-oldest`, `title`, `title-desc` (`timeSortsOrderEverySectionAlikeAndTheIdsRoundTrip`).
- [ ] Created and Updated are drawn as the row's two right-hand columns and hold the date part of
  the card's `created` / `updated`; both go with their labels when the pane is too narrow (live:
  the dates in the rows of `03`–`05`, and 0 of them in `09-narrow.png`).
- [ ] A column sort still refuses a manual reorder, with a notice pointing at the header, and
  Manual writes the rank again (the test; live: `06-notice.png`).
- [ ] The section list's order: a drag handle and ▲ ▼ per row, a drop above or below the row it
  lands on, `columns:` rewritten in the new order, no card moved, Verified and Done fixed and not
  draggable (`movingASectionRewritesColumnsAndNeverACard`,
  `theRowsMoveWithTheButtonsAndTheDropLandsWhereItWasDropped`; live: the page in `07-sections.png`).
- [ ] The pane still opens with its saved sort, its folds and its hidden sections.
- [ ] Targeted suites: `ctest --test-dir build -R board` and `-R "theme|buttonfit|hints"` pass.
