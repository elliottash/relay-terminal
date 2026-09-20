# Implementer evidence — #3XZV improve switchboard sections

Date: 2026-09-20 · implementer: glm/glm-5.3 · commit: 4f5acd43
(`board.py`'s statuses and `section` field landed earlier inside 81ff60f8,
swept there by the #VKFV session's land while this card's edits were
uncommitted — same net content.)

The owner asked for no test suites this round ("no tests, just try to deliver
efficiently, commit, and move to needs QA"), so the evidence is the build, the
worker smoke checks and the board's own validator.

## What was verified by running it

- `scripts/relay-build` — green, all targets, 51 s, on the tree that landed
  (and land.py built the exact landing tree again before committing it).
- Worker smoke (`PYTHONPATH=backend python3`): `column_statuses_of` reads an
  explicit empty list as a manual section and `planning` as its own column;
  `planning`/`planned`/`executing`/`needs-verification` are work statuses with
  no state subfolder; `_column_statuses` accepts `{research: []}` and refuses
  an unknown status in it; `STAGE_MOVES` maps `plan-written` → `planned`;
  `DEFAULT_CONFIG["columns"]` is the new eight.
- `scripts/relay-board.py check` over this repo's migrated board: 303 cards,
  no new problems — the two it reports (`bad_id` BVL1, and a
  `task_missing_marker` warning on an old QA card) are pre-existing on `HEAD`
  and unrelated to this change.

## What a verifier should still do (the QA checklist on the card)

The live GUI walk and the two test files the plan named — see the card's
`## QA checklist`.
