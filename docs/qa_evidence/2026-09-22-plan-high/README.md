# PH9G implementation evidence

Entering Plan now selects the High role through the same Pane method as /high.
The selection is remembered before lazy configuration and replayed if configuration
was already in flight. Repeated Plan does not toggle High off; Build keeps the selection.
The default backend planning resolver uses the active model, preserving the Main
role's base so /main can return to it. Explicit planning overrides still apply.

Verification:

- `scripts/relay-build --target relay-consolemode-tests relay` passed (build 2026-09-22.20H.04).
- With a fresh temporary `XDG_CONFIG_HOME`, `xvfb-run -a ctest --test-dir build -R '^consolemode$' --output-on-failure` passed, 1/1. Real Pane tests check High-before-Plan protocol order, repeated Plan, exit to Build, and configuration already in flight.
- `PYTHONPATH=backend python3 -m unittest tests.test_plan_turns tests.test_roles` passed, 112 tests. The added recording-provider case selects GLM as High from a Kimi Main base, executes a plan prompt, and verifies GLM served it while Main still holds Kimi.
- `python3 scripts/relay-board.py check` found no PH9G findings; repository-wide validation has 12 pre-existing errors on unrelated records.

Independent UI verification remains for the board verifier.
