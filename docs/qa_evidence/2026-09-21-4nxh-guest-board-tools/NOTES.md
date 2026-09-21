# Card #4NXH — implementer evidence

The pane-owned `relay_board` MCP server exposes exactly board_list, board_read,
board_comment, board_update_card and board_move_card. It reuses the live Agent's
prepare/execute path and BoardTools rather than constructing a second policy context.

## Automated checks

`PYTHONPATH=backend python3 -m unittest tests.test_guest_board_bridge tests.test_guest_harness_codex tests.test_guest_harness_claude tests.test_guest_harness_provider tests.test_board_tools tests.test_board_turns tests.test_board.PolicyFileTests`

496 tests passed. The bridge tests exercise actual Unix socket/stdio transport, schemas,
capability isolation, native plan/read-only/card-scope refusals, write budgets, evidence and
verdict gates, worker attribution, one-write retry behavior, changed-payload rejection,
turn revocation, cancellation, cleanup, native event labels and policy fallback.
Adapter checks cover launch configuration and start/resume/fork overrides. Existing model/
effort/relaunch fixtures remain green. No automated test starts a paid guest.

## Live clients

`codex-live.json` and `claude-live.json` contain the provider events and disposable-board
thread/body captured by the implementer. Both clients discovered the server before their
first model turn, used all five tools, added exactly one requested progress comment,
appended a QA checklist and moved the card to Ready. Both rediscovered using fresh
capabilities after closing and resuming the guest session. Exact models: gpt-6-astra and
claude-fable-5-1. Thread entries name those models and the Relay session/turn.

The final captures also show native board tool labels, with one started/result event per
invocation. Claude additionally uses its own ToolSearch to discover the MCP tools.
No production board content was used for these acceptance calls.

These are headless harness/provider checks, not GUI screenshots. The card's QA checklist
retains live pane rendering, model/effort switching and interactive Stop/new-turn checks
for the verifier; the relevant lifecycle and policy paths have automated coverage.

## Repository validation

`python3 scripts/relay-board.py check` reports 2 existing board errors and 748 warnings
across the shared board, including the pre-existing MDL1 thread-name/order findings.
The implementation does not edit unrelated cards to silence those findings.

The Switchboard targeted runner also recorded these cases in local test history
(run `20260921T124408Z-e7e3`). `TestsCommands.check_card("4NXH")` then returned
no findings, failing tests or blocking signals.
