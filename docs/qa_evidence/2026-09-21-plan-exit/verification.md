# Agent request to exit plan mode — #XP7N

Implemented exit_plan_mode using the existing question and mode_changed events; no GUI or wire format changes.

## Automated verification

- `PYTHONPATH=backend:tests python3 -m unittest test_sessions test_questions -q`: 92 passed.
- Relay TestsCommands runner selected all 9 PlanModeTests: 9 passed, no signals opened (run 20260921T190001Z-61a6).
- TestsCommands.check_card("XP7N"): no findings, actions or blocking signals.
- Acceptance proves an edit is refused before the ask and succeeds afterwards in the same turn; mode is saved as build. Refusal, ambiguous/missing answers, cancellation and the question cap keep plan restrictions. Invalid arguments, build mode, readonly turns and unreachable users are refused.
- Combined sessions/questions/plan-turn run before the final cap test: 122 passed, one failed. GuestPlanTurnTests.test_a_plan_turn_on_a_glm_pane_runs_through_codex_and_comes_back fails its PLAN MODE prefix assertion on clean baseline a63f33f1542f as well. Tracked separately in #GPF7.
- Board format check: no findings for either new card or thread; existing board has unrelated findings.
- `git diff --check`: passed.

## UI inspection and remaining verification

Inspected src/Pane.h: existing question handler calls showQuestion; mode_changed updates m_agentMode and displays Build mode. No C++ changes. Live GUI acceptance has not been performed; card QA checklist covers the inline ask, mode indicator, refusal and Stop. This tool is for native Relay agents; one-shot guest planners retain their existing plan-only contract.
