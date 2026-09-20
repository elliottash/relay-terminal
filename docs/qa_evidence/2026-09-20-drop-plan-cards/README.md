# #X7NB — drop the plan card type (2026-09-20)

Owner, 2026-09-20: **"drop plan cards."** `CARD_TYPES` declared a `plan` type with format, folders
and fields and no runtime behaviour; the protocol already said a plan is the `## Plan` section of
the work card it plans. There were zero `type: plan` cards on this board, so nothing was orphaned.

## Files

| File | What it shows |
|---|---|
| `no-plan-card-type.txt` | A live run on a throwaway board: `CARD_TYPES` is `('work', 'memory', 'alias')`; `PLAN_FOLDER`, `PLAN_FIELDS` and `PLAN_STATUS_FOLDER` are gone; a card file declaring `type: plan` is refused as `unknown_type: type 'plan' is not one of ('work', 'memory', 'alias')`; `board_create_card {type: "plan"}` answers "type must be one of work, memory, alias."; a work card still lives in the `planning/` tab; the Plan **turn mode**, `PLAN_HEADING` and the `plan` thread-entry kind are untouched; an alias card round-trips active → retired → active (#W3KD); an issue footer no longer carries plan links. |
| `unittest-board.txt` | `RELAY_KEYRING=off PYTHONPATH=backend:tests python3 -m unittest tests.test_board tests.test_board_tools tests.test_forge_sync` — 426 tests, OK. |
| `ctest-board.txt` | `ctest --test-dir build -R board` — board, boardsections, boardworkspace, boardpane all pass. |
| `relay-board-check.txt` | `scripts/relay-board.py check` — 338 cards, 0 errors (the 2 warnings are other cards' missing task markers, unrelated). |

`tests/test_board_protocol.py` has two failures (`'discussing' != 'inbox'` and a thread round-trip);
both reproduce on a clean `git archive HEAD` export, so they predate this change and are not part
of it.

## What was deliberately kept

The `## Plan` **section** and everything in `stage_advance` that reads it; the Plan **turn mode**
(`CARD_MODES`, `CARD_MODE_BOARD_TOOLS["plan"]`, the `mode` plumbing in `BoardModel.cpp` and
`BoardPane.cpp`); the `plan` **thread-entry kind**; the `planning/` **tab**, which holds work
cards; plan mode's own files under `<root>/.relay/plans`; and `links.plans` in the front-matter
schema, which 331 cards carry and which is now inert.
