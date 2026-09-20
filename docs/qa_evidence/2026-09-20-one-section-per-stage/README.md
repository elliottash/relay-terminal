# Evidence — #Z4HR One section per stage (2026-09-20)

- `unittest-board.txt` — `python3 -m unittest tests.test_board tests.test_board_tools`
  (370 tests, OK), including the new cases: `unknown_section` warns and is not fixable,
  schema sections and memory/alias cards do not warn, `## Resolution` no longer closes a QA
  card, `AGENT_SECTIONS` is `CARD_SECTIONS` minus `issue`, and rewriting an out-of-schema
  heading logs the rewrite.
- `relay-board-check.txt` — `scripts/relay-board.py check` on the live board: 358 cards,
  **0 errors**, 737 warnings (734 `unknown_section` — the migration backlog the card's plan
  predicted, now visible and countable; 3 pre-existing `task_missing_marker`).
