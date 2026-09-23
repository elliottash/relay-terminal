# Guest Plan exit, #XP7N

The guest MCP bridge now advertises `write_plan` and `exit_plan_mode` and dispatches both through `Agent._prepare` and `Agent._execute`. A routed Codex guest starts with ordinary permissions, saves a plan, exits Plan, and completes implementation in the same scripted harness turn. Relay emits `plan_written` once and `mode_changed {mode: "build"}`; the final implementation reply is not saved as another plan. The bridge rejects exit in Build and on a readonly turn.

## Commands

`PYTHONPATH=backend:tests python3 -m unittest tests.test_guest_board_bridge.BridgeTests.test_guest_can_write_plan_and_exit_in_same_turn tests.test_plan_turns.GuestPlanTurnTests tests.test_sessions.PlanModeTests -q` — 28 tests passed.

`PYTHONPATH=backend:tests python3 -m unittest tests.test_guest_board_bridge tests.test_plan_turns tests.test_sessions.PlanModeTests -q` — 58 tests ran; one existing `test_provider_turn_binds_native_context_and_revokes_on_failure` assertion failed because it expects `relay_board` in the turn prompt. The harness receives that instruction through `harness.instructions`; its turn prompt is `comment on the card`. The new guest Plan tests passed in this run.

Live GUI validation remains for the separate verifier: confirm the PLAN indicator clears and the same guest turn can edit after exiting.
