---
id: 1AA6
type: work
status: needs-verification
labels: [feature, switchboard, board, qa]
assignee: agent
component: [worker, gui]
parent: BX7B
blocked_by: [WFRA]
rank: zzzzzzzzzzzzzzzzzzz
created: '2026-09-23'
source: owner, Relay conversation, 2026-09-23
implemented_by: anthropic/claude-fable-5-1
verify: {artifact: code, primary: script, also: [ai-text], human: optional, criteria: each refusal names the card and the missing thing in one sentence, sign_off: none, effort: medium}
links: {plans: [], commits: [c40c6695254620c6ee282b7b1f56802d68fedef2, e5e69ff78ea88ca0458d43c68f1bb1f02492a579, 65b0f6ffb5d56fb8569e6ef76c2a05aef7b6ea16], evidence: [], related: [BX7B, 1QKM, JNYN], github: null}
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

## Plan
**Goal.** One definition of verified, and three refusals that use it.
**Findings.** `board_move_card` already funnels every status change through `_signal_gate` and `_human_qa_gate`; the `## Human QA` parser lived in `board_tools`, the `### Check` block format in `tests_protocol` (`- <status> · <test> — …`, statuses passed / failed / missing-evidence / not-applicable). So `verified()` sits in `board.py` beside the `verify` block, the parser moves down to it, and a third gate joins the two.
**Steps.** 1. `human_qa_questions`, `has_verdict`, `check_passing`, `has_receipt`, `unverified_reasons`, `verified` in board.py; `check` warns `not_verified` on a done card whose block is not met. 2. `_verify_gate` in `board_tools._move`: deferred (done and QA lanes, anyone), human required (agents; offer `needs-qa-human`), sign-off receipt (anyone); the existing open-question refusal names the block. 3. Clearing `deferred` through `fields.verify` writes who cleared it into the update's thread event. 4. `deliver` step 5, `board_move_card` description, protocol 19.21. 5. Tests for each refusal and each pass.

## Tasks
- [x] `verified()` and `unverified_reasons()` in `relay_core.board`, one `## Human QA` parser <!-- t:9j -->
- [x] `board_move_card` refusals: `verify_deferred`, `human_qa_answer` + `offer`, `receipt` <!-- t:7k -->
- [x] Clearing `deferred` recorded under the actor's name <!-- t:pc -->
- [x] `check` warns `not_verified`; row cell shows `unverified until …` <!-- t:m0 -->
- [x] Skill, tool description, protocol text; tests <!-- t:s9 -->
- [ ] Card-page strip shows "unverified until …" (GUI session) <!-- t:2v -->

## Execution Summary
Commits c40c6695 (code, tests, skill), e5e69ff7 (repair: c40c6695 had also carried the `profile:` frontmatter another session left uncommitted in `deliver/SKILL.md`; taken back) and 65b0f6ff (the skill's step-5 paragraph alone). `relay_core.board.verified(card)` is the one definition; `unverified_reasons(card)` lists what is missing in ladder order: not deferred, primary evidence (`## Verdict` or a `### Check` under `## Tests` whose lines are all `passed` / `not-applicable` with one `passed`), the person's `Answer:` when `human: required`, a `Receipt:` line in `## Verdict` or `## Execution Summary` when `sign_off` ≠ `none`; an invalid block is itself a reason. `board_move_card` (`_verify_gate`, after `_human_qa_gate`) refuses with `board_refused`: `requires: verify_deferred` (+ `until`) for `done` or a `needs-qa-*` lane while deferred, for any actor; `requires: human_qa_answer`, `offer: needs-qa-human` for an agent's `done` under `human: required` with no answered question (the owner at the keyboard is the answer, as before); `requires: receipt`, `sign_off` named, for `done` without a receipt, for any actor. The existing open-question refusal is kept and now says `(verify.human: required)` and offers the lane. Clearing `deferred` via `fields.verify` appends `verify.deferred cleared by <actor> (was …)` to the update's thread event. `check` warns `not_verified` on a `done` card whose block is not met. `board_list` rows and `BOARD.md` show `unverified until <text>`; the card-page strip is the GUI session's.

## Tests
- `PYTHONPATH=backend python3 -m unittest tests.test_board.VerifiedTests`
- `PYTHONPATH=backend python3 -m unittest tests.test_board_tools.VerifyGateTests`
- `PYTHONPATH=backend python3 -m unittest tests.test_board tests.test_board_tools tests.test_skills` — also on a clean export of 65b0f6ff
- `PYTHONPATH=backend python3 -m unittest tests.test_system_prompt.SizeTests.test_the_board_policy_block_stays_tiered`
