---
id: 1AA6
type: work
status: planned
labels: [feature, switchboard, board, qa]
component: [worker, gui]
parent: BX7B
blocked_by: [WFRA]
rank: zzzzzzzzzzzzzzzzzzz
created: '2026-09-23'
source: owner, Relay conversation, 2026-09-23
links: {plans: [], commits: [], evidence: [], related: [BX7B, 1QKM, JNYN], github: null}
---
# Verified means the human step too: `verify.human: required` gates done, and deferred verification is an honest state

## Issue
i think i want to expand the verification concept . a card has a designation of whether human QA is needed. in that case, "verified" will require that.

[...] yes, document it, and lets build all the functionality, and we can experiment with how to phase in complexity without overwhelming the user

## Done means
- With `verify.human: required`, `board_move_card` to `done` is refused (code `board_refused`, message naming the card and the missing answer) unless `## Human QA` holds a question with an `Answer:` line under it; the move it offers instead is `needs-qa-human`. `optional` and `none` do not gate. Existing behaviour (an unanswered `## Human QA` blocks close) is kept and now has a reason on the card.
- With `verify.sign_off` ≠ `none`, `done` is refused unless the card's `## Verdict` or `## Execution Summary` contains a line beginning `Receipt:`; the refusal says which sign-off is required.
- With `verify.deferred` set, `done` and `needs-qa-*` are refused; the board row and the card strip show "unverified until <text>"; clearing `deferred` (via `fields`) is the only way on, and the thread records who cleared it.
- `verified` is defined in one place (`relay_core.board.verified(card)`) and used by the move rules, `relay-board.py check` and the GUI strip: primary evidence present (a `## Verdict` or a passing `### Check` under `## Tests`), human answered when required, receipt present when required, not deferred.
- Tests in `tests/test_board.py` and `tests/test_board_tools.py` for each refusal and each pass; failure shows as a card reaching `done` with `human: required` and no answer.
