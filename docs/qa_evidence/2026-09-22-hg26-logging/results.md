# HG26 package 1 — instrumentation evidence

Implemented bounded outcome classification in `backend/relay_core/tool_outcomes.py`, consumed by `Agent._record_tool` for native and guest records. Guest adapters preserve structured command exit/deadline/refusal metadata. Normal agent wait polling and background commands are pending; ambiguous failures are unknown. Error codes come from a fixed allowlist, without logging result prose.

Worker logs append origin/run/build identity while preserving the existing event position. Explicit test entry points (`scripts/test.sh`, `relay_core.junit_runner`, and the direct role-test worker fixture) provide a temporary XDG directory to workers. Importing backend logging never mutates the environment. Explicit test-owned XDG directories are preserved when origin is already test. Backend build identity defaults to a cached source-tree hash, or the supplied packaged/QA identity. GUI worker-exit records use the actual captured running build identity.

`Pane.h` changes only the worker-finished logging block: lifecycle shutdown/reconfigure at INFO, unexpected termination at ERROR, with reason and expected flag. No model/provider switching hunks changed.

Implementation commit: `5304f3a244f829c1b79305f0a6225578089b00a2` on main. The landing tool built the exact proposed tree successfully before updating main; all scoped paths were clean afterwards.

## Validation

- Exact-tree landing gate: tree `a7d536f2e6f5`, configured and built `relay` successfully in the land tool's isolated verification directory.
- Live recorder → parent report integration: one pending wait and one nonzero command parsed as two QA-origin calls, matching outcomes, zero skipped records. The first ad hoc assertion incorrectly expected a top-level `tool_calls` key; corrected to sum each tool row's `calls` and reran successfully.

- `PYTHONPATH=backend:tests python3 -m unittest test_tool_outcomes test_logs test_guest_harness_codex.ToolTurnTest test_guest_harness_claude.BashTurnTest test_guest_harness_provider.TurnTests test_tools.ToolTests.test_timeout test_tools.ToolTests.test_command_output_and_exit test_roles -v`: **123 tests passed**; `tests.txt`.
- `RELAY_SESSION=hg26-logging scripts/relay-build --target relay`: passed; build `2026-09-22.12H.02`. Existing missing-initializer warning at `Pane.h` ForkText is unrelated.
- `python3 docs/qa_evidence/2026-09-22-hg26-logging/gui.py`: passed on isolated Xvfb/profile/workspace, no provider calls. `worker-exits.log` shows intentional killed reconfigure at INFO, unexpected clean exit at ERROR, and clean pane shutdown at INFO. The fixture's first draft raised RuntimeError (not a recognized configure-recovery exception); changed it to AttributeError to exercise the intended restart path.
- The isolation regression launches a real child worker logger with an inherited live pane ID, proves origin=test, and proves the original data directory received no log files.
- Native and guest command failures, wait polling, and timeout classifications agree; privacy regression asserts error prose/preview and arbitrary error codes never enter diagnostics.

## Scope and limitations

Relay board/delegation/message tools were absent from discovery. Parent owns all HG26 card/thread updates; no further delegation was attempted. Historical missing metadata remains unknown; arbitrary error strings are deliberately not classified by prose. Typed native connection/timeout catches now supply fixed codes. The existing guest dispatch exception boundary supplies connection/timeout/internal codes where exception type establishes the category; AttributeError/NameError map to internal_error. Generic filesystem and encoding errors remain unknown. No broader exception handling or error-prose guessing was added.

## Parent review follow-up

The initial implementation's import-time XDG mutation was removed after parent review. Test isolation is explicit and scoped to test execution; the regression verifies imports leave the entire environment untouched, the runner restores it, an inherited live pane ID does not defeat isolation, and test-owned directories are preserved. Native exception injection and a real guest bridge dispatch verify safe category producers. Metadata remains after EVENT, compatible with the parent report parser.

Validation: `PYTHONPATH=backend:tests python3 -m relay_core.junit_runner -s tests test_tool_outcomes test_logs test_roles test_junit_runner test_guest_board_bridge test_agent.AgentTests -q` (see `tests-scope.txt`).
