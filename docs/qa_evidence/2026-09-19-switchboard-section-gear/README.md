# The section gear — implementer evidence (2026-09-19)

Owner: *"put a gear after the list of switchboard sections, which allows you to add, remove, merge,
or rename sections."*

`drive.sh` boots Relay under Xvfb with an isolated `HOME`, `XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR` and
`TMPDIR`, on a throwaway project whose `.switchboard/` has one card in each lane. No provider and no
model: every scene here is the pane's own UI and the worker's `board.yaml` write. The gear's
position is read off `implementer-list.png` and passed back in (`GEAR_AT=x,y`, `NAME_AT`, `SAVE_AT`),
because the row is a flow layout and nothing in it has a fixed place.

| Shot | Claim |
| --- | --- |
| `implementer-list.png` | The gear ⚙ at the end of the section checkboxes, after DONE — after the list of sections, not among them |
| `implementer-page.png` | The page it opens, in the pane: a row per section (name, the statuses it collects, Merge…, ✕), the add row, and Save disabled with "Nothing changed yet." Verified and Done have Merge…/✕ greyed out |
| `implementer-renamed.png` | "Ready to start" renamed to "Up next" in its row: the footer says what will be written and Save has come alive |
| `implementer-saved.png` | After Save: the section header reads **UP NEXT** and its checkbox reads **UP NEXT** — no reopen, and the card is still in it |
| `board-after-save.yaml` | What was written: `column_titles: {ready: Up next}` and `columns:` untouched. The status id is still `ready`, so no card file moved |

## Two fixes this run found

1. **The checkbox kept the old name.** `syncSectionChecks()` rebuilt the row only when the section
   *ids* changed, and a rename changes no id: the header said "UP NEXT" over a box that still said
   "READY TO START". It compares the titles too now, pinned by
   `aRenameReachesTheCheckboxAsWellAsTheHeader`.
2. **The statuses column repeated the name.** For a one-status section it read "Inbox   Inbox".
   It shows the status *ids* (`inbox`, `needs-qa-llm, needs-qa-human`) — the thing a rename does
   not change, which is what the column is there to say — with the titles in its tooltip.

Esc closes the page and writes nothing (`escLeavesTheSectionsAsTheyAre`); the page takes focus when
it opens so that works from the first keystroke.
