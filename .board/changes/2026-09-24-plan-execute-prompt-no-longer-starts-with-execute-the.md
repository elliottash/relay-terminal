---
id: P4XN
type: work
status: planned
labels: [bug, tests, protocol]
rank: zzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
source: pane bd9e4ae0 delivering
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# plan_execute prompt no longer starts with "Execute the plan" — test fails on plain HEAD

## Issue

`tests.test_session_protocol.ProtocolHandlerTests.test_compact_resume_recap_and_plan_execute`
fails on a clean export of main, before any local change. Measured 2026-09-24, main at
`79dd56a`:

```
git archive HEAD | tar -x -C /tmp/pristine
cd /tmp/pristine && PYTHONPATH=backend python3 -m unittest \
  tests.test_session_protocol.ProtocolHandlerTests.test_compact_resume_recap_and_plan_execute
→ FAIL: self.assertIn('Execute the plan in ', sent[-1]['content'][:24])
 AssertionError: '# Plan Restore Plan' does not contain 'Execute the plan in '
```

The prompt the worker sends after `compact → resume → recap → plan_execute` now begins with
`# Plan Restore Plan` instead of `Execute the plan in …`. Either the plan-execute opening
regressed on main (the agent is handed a plan title where it used to be handed an instruction),
or the opening was deliberately reworded and this test was not updated with it. Which side is
right needs the owner or the session that changed the plan opening; not fixed in passing while
delivering #Q8TM.

## Done means
`tests.test_session_protocol.ProtocolHandlerTests.test_compact_resume_recap_and_plan_execute`
passes on a clean export of current `main` (measured the way the issue did: `git archive HEAD`,
then the single unittest under `PYTHONPATH=backend`). The plan-execute prompt's opening line is
again an explicit instruction consistent between code and test — whichever side was corrected,
the commit that changed the wording is cited in the Execution Summary so the choice is on the
record. Failure is recognised by: the test failing on a clean export, or the prompt handed to the
agent opening with the plan's own Markdown title (`# Plan …`) instead of an instruction.

## Plan
## Goal

Make the plan-execute prompt and its test agree again, on the record: find out whether the
prompt's opening was deliberately reworded, then correct whichever side regressed.

## Findings

- The prompt is built by `planning.execution_prompt(path, content)` in
  `backend/relay_core/planning.py:248`, called from `SessionCommands._plan_execute` in
  `backend/relay_core/session_protocol.py:1669` (with a separate long-plan fallback at line ~1674).
- On the current working tree the prompt opens `Run the plan in {path}:` and appends
  `ORCHESTRATION_NOTE` (#K3TY). The long-plan fallback in `_plan_execute` also says
  "Run the plan in", i.e. consistent with the current `execution_prompt` — a hint the
  rewording away from "Execute the plan in" was deliberate, not an accident.
- The test asserts the old wording: `tests/test_session_protocol.py` ~line 343,
  `self.assertTrue(sent[-1]['content'].startswith(f'Execute the plan in {plan}'))`.
- The issue measured the failure at main `79dd56a` with the prompt opening
  `# Plan Restore Plan` — neither the old nor the current wording — so the wording has moved at
  least twice and the measured HEAD may already be stale. Re-measure before deciding.

## Steps

1. Reproduce on a clean export of current `main`:
   `git archive HEAD | tar -x -C <scratch>`, then
   `PYTHONPATH=backend python3 -m unittest tests.test_session_protocol.ProtocolHandlerTests.test_compact_resume_recap_and_plan_execute`.
   Record what the prompt actually starts with now.
2. Archaeology: `git log --oneline -S 'Execute the plan in' -- backend/relay_core/planning.py`
   and the same for `'Run the plan in'` (and, if it still shows, `'# Plan Restore Plan'`), plus
   `git log -p` on the test's assertion line. Read the commit messages and any cards they cite
   to establish whether the rewording was a deliberate product change.
3. Fix the side that is wrong:
   - **If the rewording was deliberate** (expected, per Findings): update the assertion in
     `tests/test_session_protocol.py` to the current opening, keeping the neighbouring
     `'edited by user'` and single-user-message assertions. `rg 'Execute the plan'` to catch any
     other test or doc asserting the old string and update those too.
   - **If it was a regression**: restore an instructional opening in
     `planning.execution_prompt` (keeping the `ORCHESTRATION_NOTE` append and the
     `_plan_execute` fallback consistent with it) and leave the test alone.
4. Re-run the test on the clean export (not just the working tree) until it passes, and run
   `tests/test_planning.py` (or whichever file covers `execution_prompt`) alongside it.
5. Land with `scripts/land.py` (`begin` the touched files, `commit`); record the deciding commit
   from step 2 and the test verdict in the Execution Summary.

## Risks

- The deciding question — was the rewording deliberate? — is answered by git history, not by
  preference; if history is ambiguous (a wording change with no message or card), post a
  `question` comment on this card and stop rather than guessing.
- Another session may be editing `planning.py` or the test file: follow the shared-checkout
  rules (`git status` first, claim only the files touched, no full suite run).

## Verify

- The single unittest from Done means, run on a clean export of `main` after the fix.
- Any other test that mentions `execution_prompt` or the old opening (`rg` for both).
- How to see it working: the failure line from the issue (`AssertionError: '# Plan Restore Plan'
  does not contain …`) no longer appears.
