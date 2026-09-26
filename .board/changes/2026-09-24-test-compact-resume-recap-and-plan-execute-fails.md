---
id: BCJF
type: work
status: needs-verification
labels: [bug, tests, sessions]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 33661be1-24ff-494d-85df-72b15a2410ad
rank: zzzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
links: {plans: [], commits: [9f9584aeaa64, 9d31259af24e, a0041445b6d3, bc511a6b93f2], evidence: [docs/qa_evidence/2026-09-25-bcjf-plan-execute-test-wording/evidence.md, docs/qa_evidence/2026-09-25-bcjf-plan-execute-test-wording/], related: [P4XN], github: null}
---
# test_compact_resume_recap_and_plan_execute fails on main

## Issue
test_session_protocol.ProtocolHandlerTests.test_compact_resume_recap_and_plan_execute fails: sent[-1]['content'] does not start with 'Execute the plan in <plan>'. Reproduces deterministically (3/3) in this checkout AND on a pristine `git archive HEAD` export with no working-tree edits (backend/ tests/ only), so it is broken on main itself, not on local uncommitted work. Found while running suites for #12JX; unrelated to it (that test builds SessionCommands with no subagents).

## Done means
`tests.test_session_protocol.ProtocolHandlerTests.test_compact_resume_recap_and_plan_execute` passes on a clean export of current `main` (`git archive HEAD`, then the single unittest under `PYTHONPATH=backend`), and `tests.test_sessions`' direct `execution_prompt` test still passes beside it: code and test agree that the plan-execute prompt opens `Run the plan in <path>:` — the wording #BGRN's Execute→Run sweep deliberately introduced. The duplicate card #P4XN (same failure, planned the same day) is folded into this one so the record is a single card, and the Execution Summary cites the #BGRN commit as the deciding evidence. Failure is recognised by: the assertion failing again on a clean export, or any test asserting a plan-execute opening that `planning.execution_prompt` does not produce.

## Plan
**Goal:** make the failing test and the code agree on the plan-execute prompt's opening — the evidence says the code is right and the test is stale, so this is a one-line test fix, landed on the record.

**Findings:**
- The prompt is built by `planning.execution_prompt` (`backend/relay_core/planning.py:248-249`): `f"Run the plan in {path}:\n\n{content}\n\n{ORCHESTRATION_NOTE}"` (`ORCHESTRATION_NOTE` appended per #K3TY). Called from `SessionCommands._plan_execute` (`backend/relay_core/session_protocol.py:1683`); the >128 KiB fallback at `:1685` also says "Run the plan in" — both call sites consistent.
- The rewording was **deliberate**: it is part of #BGRN's Execute→Run terminology sweep (card in needs-verification, commits `854c097d`…), and `tests/test_sessions.py:750-751` already asserts the new opening with the comment `# wording since #BGRN (#JKG2)` — and passes on main, which proves HEAD's `execution_prompt` says "Run the plan in".
- The stale side is `tests/test_session_protocol.py:347`, in `test_compact_resume_recap_and_plan_execute`: `self.assertTrue(sent[-1]['content'].startswith(f'Execute the plan in {plan}'))` — missed by the sweep.
- `tests/test_remote_control.py:1556` contains "Execute the plan abc" but only as a user-typed fixture string, not an assertion on the prompt: leave it.
- This card duplicates #P4XN (same failure, planned 2026-09-24, still `planned`). Its plan left deliberate-vs-regression open as a git-archaeology question; the `test_sessions.py:751` comment answers it, so no owner decision is needed.

**Steps:**
1. Fold the duplicate: `board_merge_cards` #P4XN into #BCJF (this card — it is the one being run). If the merge refuses, note it in the thread and continue; the code fix is identical either way.
2. Reproduce on a clean export: `git archive HEAD | tar -x -C <scratch>`, then in it `PYTHONPATH=backend python3 -m unittest tests.test_session_protocol.ProtocolHandlerTests.test_compact_resume_recap_and_plan_execute -v`. Record what the prompt actually opens with (expected: `Run the plan in <path>:`).
3. Fix `tests/test_session_protocol.py:347` to `self.assertTrue(sent[-1]['content'].startswith(f'Run the plan in {plan}:'))  # wording since #BGRN`, keeping the neighbouring `'edited by user'` and single-user-message assertions untouched.
4. Sweep for other stale copies: `rg 'Execute the plan' tests/ backend/ src/ docs/` — only the `test_remote_control.py` fixture should remain.
5. Verify on the clean export from step 2 (re-`git archive` after the commit, or test the landed sha's tree), plus `PYTHONPATH=backend python3 -m unittest tests.test_sessions tests.test_session_protocol -q` in the checkout.
6. Land with `scripts/land.py begin <me> tests/test_session_protocol.py` then `commit`; Execution Summary cites the #BGRN sweep and `test_sessions.py:751` as the deciding evidence and records the test verdicts; move to needs-verification with the clean-export output as evidence.

**Risks:**
- If step 2 shows an opening that is *not* `Run the plan in <path>:` (the 2026-09-24 morning measurement saw `# Plan Restore Plan` at main `79dd56a`), do not update the test to whatever appears: first run `git log --oneline -S 'Execute the plan in' -- backend/relay_core/planning.py` and `-S 'Run the plan in'` to confirm the change was deliberate, and post a `question` comment here if history is ambiguous. Low risk — `test_sessions.py:751` passing on main pins the current wording.
- Shared checkout: another session may hold `tests/test_session_protocol.py`; `land.py` will say so at `begin`/`commit` — claim only that file, no full-suite run.

**Verify:** the single unittest on a clean export of the landed sha; `tests.test_sessions` (covers `execution_prompt` directly at `:750`); the whole of `tests.test_session_protocol` in the checkout. Working = the issue's failure (`… does not start with 'Execute the plan in …'`) no longer appears and no test asserts an opening the code does not produce.

## Execution Summary
- `9f9584ae`: `tests/test_session_protocol.py:371` (the line had moved from :347) now asserts `startswith(f'Run the plan in {plan}:')  # wording since #BGRN`. Neighbouring `'edited by user'` and single-user-message assertions untouched. No code change.
- Deciding evidence: `854c097d` (#BGRN) reworded `planning.execution_prompt` Execute→Run deliberately; `f7df0d36` (#JKG2) updated `tests/test_sessions.py:751` to match but missed this test. Reproduced on a clean export of `a4584ec3`: the prompt opens `Run the plan in <path>:` (not `# Plan …`), so the plan's guard branch was not needed.
- Duplicate #P4XN: no merge tool is exposed to this guest harness, so it is folded by cross-link (`links.related`) and a thread note on both cards rather than a board merge.
- Evidence: `docs/qa_evidence/2026-09-25-bcjf-plan-execute-test-wording/evidence.md` (landed `9d31259a`).

## Tests
- Clean export of landed `9f9584ae`: `PYTHONPATH=backend python3 -m unittest tests.test_session_protocol.ProtocolHandlerTests.test_compact_resume_recap_and_plan_execute tests.test_sessions -q` → 58 tests OK (before the fix, on `a4584ec3`: FAILED, `startswith('Execute the plan in …')` False).
- Checkout: `PYTHONPATH=backend python3 -m unittest tests.test_sessions tests.test_session_protocol -q` → 96 tests OK.
- Sweep `grep -rn 'Execute the plan' tests/ backend/ src/` → only the user-typed fixture at `tests/test_remote_control.py:1556`.
