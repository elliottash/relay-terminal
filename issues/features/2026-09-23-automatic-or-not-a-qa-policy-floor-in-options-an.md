---
id: C3Q2
type: work
status: needs-verification
assignee: agent
labels: [feature, switchboard, options, qa]
component: [worker, gui]
parent: BX7B
blocked_by: [WFRA, 1AA6]
rank: zzzzzzzzzzzzzzzzzzzr
created: '2026-09-23'
source: owner, Relay conversation, 2026-09-23
verify: {artifact: code, primary: script, also: [person], human: optional, criteria: "Options › Agent shows one Verification row under QA, and a fields.verify with stakes money and human none comes back human required with a qa_policy note", effort: medium, stakes: rework, blast: capability}
links: {plans: [], commits: [71ed6ea3, ea29e766, dd00c838, 33a8f746, 71147e17], evidence: [], related: [BX7B, 1QKM, 3KB7], github: null}
---
# Automatic or not: a QA policy floor in Options and per-project overrides in board.yaml

## Issue
per project, or per globals (or options?), user can decide wherhe its automatic or not.

[...] yes, document it, and lets build all the functionality, and we can experiment with how to phase in complexity without overwhelming the user

## Done means
Owner steer, 2026-09-23: "ideally, most of this is just in the agent's work and the user doesn't see it directly." So the floor is mostly defaults the agent applies, and the user sees one switch.

- Options › Agent gains **one** row under a QA heading: *Verification* — `Ask me before closing any card` (default) | `Automatic when the plan needs no person`. Persisted in QSettings, sent in `configure`.
- `board.yaml` accepts `qa: {verification: ask|automatic}` plus the agent-side floor keys (`ask_at_stakes`, `ai_may_gate_after`, `sample_after`) for a project that wants to tune them by hand; the project value overrides the global one. No Options rows for the floor keys; their defaults live in code (`ask_at_stakes: money`, `ai_may_gate_after: never`, `sample_after: never`) and are documented in `POLICY.md`.
- The worker applies the floor silently: a proposed `verify` block whose `human` is below the floor for its `stakes` is raised to `required`; `ai-text` / `ai-visual` as `primary` is downgraded to `also` unless the project allows AI gating and the card's `qa` block shows a verifier outside the author's lineage; a `sample` is dropped when sampling is not allowed. All of this is reported in the tool result the agent reads, never to the user.
- In `ask` mode a card whose plan needs no person still stops at `needs-verification` for the user to close; in `automatic` mode the verifier's pass closes it and the reply says so in one line.
- Tests: `tests/test_board_tools.py` (floor raises human, AI gate downgrade, sample dropped, project overrides global, ask vs automatic close), a `settingspane_test.cpp` case for the one row. Failure shows as a `stakes: money`, `human: none` card accepted under defaults, or an `ask`-mode card closed by a verifier.

## Plan
**Goal.** One switch the user sees, a `qa:` block a project can hand-tune, and a floor the worker applies silently to every `verify` block it writes.

**Findings.** `validate_verify` / `verified()` are in `relay_core.board` (39eedbfb, c40c6695); `board_update_card fields.verify`, the claim reminder and #MSJ0's skill defaulting are in `board_tools.py`; `board.yaml` is read by `Board.config()`, already called in `BoardTools.__init__` for `agent:`; `configure` settings reach `BoardTools` through `board_protocol.parse_board` (`autonomy`, `limits`); the Agent tab rows are `choiceRow`s in `src/RelayWindowSettings.cpp`, and `Pane::requestOptions()` is what a `configure` carries.

**Steps.**
1. `relay_core/qa_policy.py`: `parse` (project over global over defaults), `apply` (the three rules), `closes_automatically`, `effective_line`; `tests/test_qa_policy.py`.
2. `board_protocol.parse_board` carries `qa`; `configure` copies its top-level `qa` into the board block; `_build` passes it to `BoardTools(qa=)`.
3. `BoardTools`: `qa_policy` from `board.yaml qa:` over `configure.qa`; `_qa_floor` on an explicit `fields.verify` before it is stored, `_apply_qa_floor` on a block a claim or update defaulted; `qa_policy` notes in the result; `board_read.qa_policy` line; `_qa_ask_gate` beside `_verify_gate` in `board_move_card`.
4. Options › Agent › QA › Verification (`qa/verification`, ask | automatic) and `requestOptions()` sending `qa.verification`; one `settingspane_test.cpp` case.
5. Docs: BOARD-FORMAT.md §4, AGENT-SESSIONS-PROTOCOL.md 19.21, `deliver` step 5, ARCHITECTURE.md.

**Risks.** #MSJ0's uncommitted test expects a referee-report profile (`primary: ai-text`) to land unfloored; under the default floor it lands as `primary: person, also: [ai-text]`, which is this card's rule. That assertion is the sibling session's to update.

**Verify.** `tests/test_qa_policy.py`, `tests/test_board_tools.py::QaPolicyFloorTests`, `ctest -R settings`.

## Execution Summary
- `backend/relay_core/qa_policy.py` (71ed6ea3): `parse` (project `board.yaml qa:` over global `configure.qa` over the defaults `verification: ask`, `ask_at_stakes: money`, `ai_may_gate_after: never`, `sample_after: never`), `apply` (the three silent rules: `human` raised to `required` at or above the stakes floor with a one-line `criteria` filled when missing; an `ai-text` / `ai-visual` primary moved to `also` with the next non-AI rung, or `person`, as primary unless AI gating is allowed *and* the card's `qa` block names a verifier outside the author's lineage; a `sample` dropped when sampling is off), `closes_automatically`, `effective_line`. Pure functions.
- Options › Agent › **QA** › *Verification* (ea29e766): one `choiceRow`, "Ask me before closing any card" (default) | "Automatic when the plan needs no person", QSettings `qa/verification`, sent by `Pane::requestOptions()` as `configure.qa.verification`. No rows for the floor keys.
- Worker (33a8f746): `board_protocol.board_block` folds `configure.qa` into the board block for `configure`, `agent_tools` and a `set_board` re-point; `BoardTools(qa=)` builds `qa_policy` from `board.yaml qa:` over it. The floor meets an explicit `fields.verify` before it is stored (the thread's change line names the block that stands) and a block a claim or update defaulted from a skill before the save; its notes go back as `qa_policy` in the tool result only. `board_read` carries `qa_policy: <effective line>`. `board_move_card`: `_qa_ask_gate` beside `_verify_gate` — under `ask` a verifying session moving a card whose plan needs no person out of `needs-verification` or a QA lane to `done` is refused in one sentence (`requires: user_close`, `offer: needs-verification`); under `automatic` it proceeds and the result says `closed automatically: …`. Owner closes, self-closes out of `executing`, cards with no block and cards whose person has answered are untouched.
- Docs (dd00c838, 33a8f746): BOARD-FORMAT.md §4 `qa:` block; AGENT-SESSIONS-PROTOCOL.md 19.21 paragraph; ARCHITECTURE.md one sentence; `deliver` step 5 two sentences and `issues/POLICY.md` regenerated from it. `board_policy.md` untouched.
- 71147e17: #MSJ0's skill-default test (333091b1 landed while this was in flight) now expects the floored block: the referee-report profile's `ai-text` primary lands as `person` with `ai-text` in `also`, and the claim result's `qa_policy` notes say so.

Not built here: the case ledger the `<N>` values of `ai_may_gate_after` / `sample_after` count against is #95VZ; until it lands `apply` is given `cases=0`, so a number reads as "not yet" (documented in BOARD-FORMAT.md §4).

## Tests
- `PYTHONPATH=backend python3 -m unittest tests.test_qa_policy` (20 tests: defaults, project over global, bad values named, the three rules alone and stacked, `closes_automatically`, `effective_line`)
- `PYTHONPATH=backend python3 -m unittest tests.test_board_tools.QaPolicyFloorTests` (12: floor raises human on `stakes: money, human: none`; AI primary downgraded; sample dropped; no note within the floor; `board_read` line; `configure.qa` global layer; project `board.yaml qa:` overrides global; bad project value named; ask-mode verifier close refused offering needs-verification; automatic close says so; owner close, self-close and no-plan card not gated; `human: required` stays the verify gate's)
- `PYTHONPATH=backend python3 -m unittest tests.test_board_protocol -k configure_qa` (`configure.qa` reaches both halves and survives a re-point)
- `PYTHONPATH=backend python3 -m unittest tests.test_board_tools tests.test_board tests.test_board_protocol` (647 pass on a clean export of 71147e17)
- `ctest --test-dir build -R '^settings$'` (`verificationIsTheOneRowUnderQa`; run by land.py's gate on the exact landed tree of ea29e766)
