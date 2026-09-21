# Agent exit from plan mode — #XP7N

First landed as an Execute / Keep planning ask (97add146); the owner then decided the agent should
leave plan mode on its own, Warp-style ("i woudl like it if the agent could decide itself ot leave
planning mode, more like warp"). Reworked: `exit_plan_mode {reason}` switches the session to build
mode directly — no question flow — emits the existing `mode_changed` event, and the turn continues
with build tools. No GUI or wire-format changes.

## Automated verification

- `PYTHONPATH=backend:tests python3 -m unittest test_sessions test_questions -q`: 89 passed.
- Acceptance proves an edit is refused before `exit_plan_mode` and succeeds after it in the same
  turn; the mode is saved as build, exactly one `mode_changed` is emitted, and no `question` event
  is raised. Invalid arguments, build mode and readonly turns are refused; the tool schema is
  identical on every request of the turn (prompt cache).

## Remaining verification

Live GUI acceptance has not been performed; the card QA checklist covers the PLAN indicator
clearing and implementation continuing in the same turn. This tool is for native Relay agents;
one-shot guest planners retain their existing plan-only contract.
