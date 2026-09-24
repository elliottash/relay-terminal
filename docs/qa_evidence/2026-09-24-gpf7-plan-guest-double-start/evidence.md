# #GPF7 — plan-turn guest harness double-start fix, 2026-09-24

## What the card asked

Both stale prompt-placement assertions assert the real contract and pass offline; the
`test_plan_turns` and `test_guest_board_bridge` modules green with no API key; `test_sessions`
green.

## What was found

1. The two tolerant test edits named in `## Plan` were already committed (working tree clean for
   `tests/test_plan_turns.py` and `tests/test_guest_board_bridge.py`; no live land session claimed
   them).
2. The named plan-turn test still failed — not on the prompt assertion but at line 381: the fake
   harness recorded **two** starts. Commit 6063dff7 (#RND7) had turned the guest-start `if` in
   `Agent._begin_plan_turn` into a `while` loop for ranked-peer failover without an exit on
   success: a guest that started fine was started a second time, the second start raised, and the
   turn then planned without the guest it had just started. Fixed by breaking out of the loop once
   `_start_plan_guest` returns a provider (commit `347678b7`, `backend/relay_core/agent.py`,
   +12/-9 around line 2970).
3. Separately, `test_sessions.PlanModeTests.test_the_execute_prompt_follows_the_plans_orchestration_block`
   failed on HEAD (verified with HEAD's `agent.py` in an overlay): 854c097d (#BGRN) reworded
   `planning.execution_prompt` from "Execute the plan in" to "Run the plan in" and left the test
   asserting the old prefix. Filed as #JKG2 and fixed in commit `f7df0d36`
   (`tests/test_sessions.py`, one line).

## Verification (no API key, FakeHarness only)

```
$ PYTHONPATH=backend:tests python3 -m unittest \
    test_plan_turns.GuestPlanTurnTests.test_a_plan_turn_on_a_glm_pane_runs_through_codex_and_comes_back \
    test_guest_board_bridge.BridgeTests.test_provider_turn_binds_native_context_and_revokes_on_failure -q
Ran 2 tests in 0.236s
OK

$ PYTHONPATH=backend:tests python3 -m unittest test_plan_turns test_guest_board_bridge test_sessions
Ran 112 tests in 4.837s
OK
```

Before the `agent.py` fix the same two-test run reported `FAILED (failures=1)` with the
double-start assertion diff; before the `test_sessions.py` fix the 112-test run reported the
`Execute the plan in` prefix failure. Both fixes are on `main` (`347678b7`, `f7df0d36`); the
112-test OK above was run with both in the working tree.
