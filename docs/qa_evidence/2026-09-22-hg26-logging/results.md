# HG26 package 1 — instrumentation evidence

Implemented bounded outcome classification in `backend/relay_core/tool_outcomes.py`, consumed by `Agent._record_tool` for native and guest records. Guest adapters preserve structured command exit/deadline/refusal metadata. Normal agent wait polling and background commands are pending; ambiguous failures are unknown. Error codes come from a fixed allowlist, without logging result prose.

Worker logs append origin/run/build identity while preserving the existing event position. Tests importing the backend under unittest/pytest inherit a temporary XDG data directory into their worker subprocesses. Explicit test-runner isolation is retained when origin is already test. Backend build identity defaults to a cached source-tree hash, or the supplied packaged/QA identity. GUI worker-exit records use the actual captured running build identity.

`Pane.h` changes only the worker-finished logging block: lifecycle shutdown/reconfigure at INFO, unexpected termination at ERROR, with reason and expected flag. No model/provider switching hunks changed.

## Validation

- `PYTHONPATH=backend:tests python3 -m unittest test_tool_outcomes test_logs test_guest_harness_codex.ToolTurnTest test_guest_harness_claude.BashTurnTest test_guest_harness_provider.TurnTests test_tools.ToolTests.test_timeout test_tools.ToolTests.test_command_output_and_exit test_roles -v`: **123 tests passed**; `tests.txt`.
- `RELAY_SESSION=hg26-logging scripts/relay-build --target relay`: passed; build `2026-09-22.12H.02`. Existing missing-initializer warning at `Pane.h` ForkText is unrelated.
- `python3 docs/qa_evidence/2026-09-22-hg26-logging/gui.py`: passed on isolated Xvfb/profile/workspace, no provider calls. `worker-exits.log` shows intentional killed reconfigure at INFO, unexpected clean exit at ERROR, and clean pane shutdown at INFO. The fixture's first draft raised RuntimeError (not a recognized configure-recovery exception); changed it to AttributeError to exercise the intended restart path.
- The isolation regression launches a real child worker logger with an inherited live pane ID, proves origin=test, and proves the original data directory received no log files.
- Native and guest command failures, wait polling, and timeout classifications agree; privacy regression asserts error prose/preview and arbitrary error codes never enter diagnostics.

## Scope and limitations

Relay board/delegation/message tools were absent from discovery. Parent owns all HG26 card/thread updates; no further delegation was attempted. Historical missing metadata remains unknown; arbitrary error strings are deliberately not classified by prose. The fixed transport/internal categories apply when structured metadata exists. This work does not broaden exception handling elsewhere to invent missing metadata.
