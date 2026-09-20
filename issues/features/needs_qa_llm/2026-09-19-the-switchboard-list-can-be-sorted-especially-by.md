---
id: SEDZ
type: work
status: needs-qa-llm
labels: [feature, switchboard]
implemented_by: glm/glm-5.3
rank: zzzzzzzzzz
created: '2026-09-19'
source: pane relay-terminal, 2026-09-19
links: {github: null, commits: [eb6909ae], evidence: [docs/qa_evidence/2026-09-19-switchboard-sort/], plans: [], related: []}
---
# The Switchboard list can be sorted, especially by time

## Issue
in the switchboard, add sorting options, especially by time.

## Plan
1. A `Sort` menu beside the filter box: **Manual** (the board's rank, the order drags and
   Alt+Shift+↑↓ write; Done and Verified stay newest first), **Newest first**, **Oldest first**
   (by `created`) and **Recently updated** (by the row's new `updated`).
2. A time sort orders *every* section alike and takes the manual reorder off — a drop inside the
   card's own section and Alt+Shift+↑↓ answer with a notice instead of writing a rank nobody
   can see; drops *between* sections still move (they write a status, not a place).
3. The worker adds `updated` to every row (protocol 19.2): the later of the card file's mtime
   and its thread file's, as an ISO UTC timestamp. An older worker sends none and the
   Recently updated sort falls back to `created`.
4. The choice is saved with the window's layout (`{"board": {…, "sort": "newest"}}`) and each
   pane restores its own.

Where: `src/BoardModel.h/.cpp` (Sort, sortId/sortFromId, Model::setSort, the tie-broken
comparator), `src/BoardPane.h/.cpp` (the menu, the button, the reorder refusal),
`src/PaneChrome.h` + `src/RelayWindow.h` (save/restore), `backend/relay_core/board_tools.py`
(`_updated_at` on the row), `docs/SWITCHBOARD-DESIGN.md` 4.6 and
`docs/AGENT-SESSIONS-PROTOCOL.md` 19.2.

## QA checklist
- `ctest --test-dir build -R '^board'` — the model and pane tests, including the two new ones
  (`timeSortsOrderEverySectionAlikeAndTheIdsRoundTrip`,
  `theSortMenuOrdersTheListAndReordersOnlyOnManual`).
- `cd tests && PYTHONPATH=../backend python3 -m unittest \
  test_board_protocol.OpenTests.test_a_row_says_when_the_card_last_changed \
  test_board_protocol.OpenTests.test_a_card_row_carries_what_the_pane_draws` — the row's
  `updated` and its shape.
- `docs/qa_evidence/2026-09-19-switchboard-sort/drive.sh` — the live run under Xvfb, whose
  `ocr.txt` is the receipt: the three orders (Bravo Charlie Alpha → Charlie Bravo Alpha →
  Alpha Bravo Charlie), the button's label following the choice, and the Alt+Shift+↓ refusal.
- Full suites on the shared tree: `ctest` and `./scripts/test.sh` pass except two faults that
  are not this card's — `buttonfit` (dark-copper 8.5pt under the 9pt floor, noted on #DKCV)
  and a `test_board_turns` flake that passes 3/3 alone (known under load).
- Restore check: close the window with a non-Manual sort on, reopen — `sort` rides the layout
  node and the pane comes back sorted (unit-tested through `sortOrder`/`setSortOrder`; the
  live run covers the menu path).
