---
id: C3Q2
type: work
status: executing
assignee: agent
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
