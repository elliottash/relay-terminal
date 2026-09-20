#76DJ — closing a QA card needs a verdict, not a different model family

Owner, 2026-09-20: "can you change that rule -- flipping to done doesnt require a different pane, just that its verified".

## What changed
- backend/relay_core/board_tools.py: move_card no longer refuses a close because the closing
  pane's model family matches implemented_by. The verdict section (## Verdict / QA Verdict /
  QA Result / Resolution) and the Relay-Free refusal (owner, 2026-09-19) stay. verified_by
  still stamps whoever closes. Tool description and QA_STATUSES comment updated.
- backend/relay_core/board.py, board_policy.md (the agent rules text), SWITCHBOARD-DESIGN.md,
  SWITCHBOARD-FORMAT.md: independence-rule wording replaced with the verdict-on-close rule.
- tests/test_board_tools.py: the same-family close test flipped to assert success + verified_by
  stamp; the relay-free-upstream test flipped likewise; the relay-free-closer refusal and the
  no-verdict refusal stay as they were.

## Test output (this machine, 2026-09-20)
----------------------------------------------------------------------
Ran 148 tests in 2.087s

OK
----------------------------------------------------------------------
Ran 258 tests in 13.441s

OK
