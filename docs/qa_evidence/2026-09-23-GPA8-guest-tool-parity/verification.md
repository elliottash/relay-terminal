# Guest tool parity verification — #GPA8

Date: 2026-09-23. All live turns used a disposable temporary workspace and Board; the
temporary files were removed after each turn. The installed Codex and Claude Code clients
ran through `guest_harness_provider.start_provider`, the pane Agent, and the stdio MCP proxy.

## Offline checks

`PYTHONPATH=backend:tests python3 -m unittest tests.test_guest_board_bridge tests.test_guest_delegation tests.test_guest_memory tests.test_guest_harness_codex tests.test_guest_harness_claude tests.test_guest_harness_provider tests.test_app_tools tests.test_keybindings tests.test_board_tools tests.test_board.PolicyFileTests -q`

Result: 668 tests passed in 15.896 seconds. The bridge tests include native catalog comparison,
scope-specific Board discovery, guest card creation and claim, app write gate refusal and allowed
write, session metadata, keybinding update, ungranted/granted program typing, and a named unittest
returning one pass. Delegation tests cover foreground completion, native 1–1800 second waits,
and Stop revoking a blocked foreground child. A slow named test is also stopped through the guest turn's cancel event; its
MCP call returns before the test's five-second sleep would finish. The pre-existing guest-prompt
test now checks the harness instructions where
Relay actually sends them; the preceding user prompt contains only the user's words.

## Installed-client turns

First turn, each guest was asked to create and read one disposable card through `relay_board`:

| Guest | MCP discovery | Card created | Observed tool calls | Outcome |
| --- | --- | --- | --- | --- |
| Codex | yes | 1 | `board_list`, `board_create_card`, `board_read` | `done`, 16.6 s |
| Claude Code | yes | 1 | `board_create_card`, `board_read` | `done`, 7.6 s |

Second turn used a disposable Board plus a fake GUI response loop and session-info source.
The guest was asked for `app_option_list`, `session_info`, `board_create_card`, `board_claim`
and `tests_check` in sequence:

| Guest | MCP discovery | Card status | Observed Relay tool calls | Outcome |
| --- | --- | --- | --- | --- |
| Codex | yes | executing | all five (creation attempted twice; one card exists) | `done`, 29.1 s |
| Claude Code | yes | executing | all five | `done`, 11.2 s |

The fake GUI established the worker/app tool round trip, not a visual UI check. Program input
was verified with a simulated delegated pane response; no keystroke was sent to the user's
terminal. The named `tests_run` integration check was offline and short; it establishes the
MCP/Board dispatch and structured verdict, not a 30-minute endurance run. Codex's per-server
timeout is configured to 24 hours for longer runs and foreground delegation; Claude Code's documented default MCP
tool timeout exceeds the native test runner ceiling.

A wider, optional run that also included `GuestPlanTurnTests` and `PlanModeTests` ran 695 tests:
the 668 parity tests passed, while four Plan-mode tests failed against concurrent shared-checkout
changes in `agent.py`, `planning.py` and `roles.py`. None of those files is part of #GPA8.
