---
id: C3Q2
type: work
status: planned
labels: [feature, switchboard, options, qa]
component: [worker, gui]
parent: BX7B
blocked_by: [WFRA, 1AA6]
rank: zzzzzzzzzzzzzzzzzzzr
created: '2026-09-23'
source: owner, Relay conversation, 2026-09-23
links: {plans: [], commits: [], evidence: [], related: [BX7B, 1QKM, 3KB7], github: null}
---
# Automatic or not: a QA policy floor in Options and per-project overrides in board.yaml

## Issue
per project, or per globals (or options?), user can decide wherhe its automatic or not.

[...] yes, document it, and lets build all the functionality, and we can experiment with how to phase in complexity without overwhelming the user

## Done means
- Options › Agent gains a **QA** heading with four rows, each persisted in QSettings and sent in `configure`: *Always ask a person for stakes at or above* (nuisance|rework|money|reputation|harm; default `money`), *AI review may gate on its own* (never | after N passes; default never), *Default sample once earned* (every case | 1 in 5 | 1 in 10; default every case), *Sign-off actions always need my click* (on, not editable, shown for honesty).
- `board.yaml` accepts a `qa:` block with the same four keys; a project value overrides the global one; `board_read`'s result and the policy prompt section state the effective values in one line.
- The worker applies them: a proposed `verify` block whose `human` is below the floor for its `stakes` is raised to `required` with a note in the tool result; a `sample` is refused when the floor says every case; `ai-text`/`ai-visual` as `primary` is downgraded to `also` unless the project allows AI gating and the card's `qa` block shows a verifier outside the author's lineage.
- Defaults are the conservative ones (always ask), so a user who never opens the heading sees no change except one extra line in refusals.
- Tests: `tests/test_board_tools.py` (floor raises human, sample refused, AI gate downgrade, project overrides global), a `settingspane_test.cpp` case for the four rows. Failure shows as a card with `stakes: money` and `human: none` accepted under default settings.
