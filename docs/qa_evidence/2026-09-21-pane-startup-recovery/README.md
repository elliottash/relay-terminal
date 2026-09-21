# Pane startup and configuration recovery — #MDL1 / #40SN

Implemented pre-start Plan/Build selection without eagerly spawning a guest. `configured` applies the remembered mode before pumping the first queued ask. Initial configuration defects report their exception text; the pane retries the same configuration in a fresh worker once, and offers Retry agent if the defect persists. Queued prompts survive.

Verification:
- `scripts/relay-build --target relay`: passed.
- `PYTHONPATH=backend python3 -m unittest discover -s tests -p test_configure_recovery.py -v`: 4 tests passed (including all four exception classes, same-process failure vs fresh-process recovery, validation errors and redaction).
- `PYTHONPATH=backend python3 -m unittest discover -s tests -p test_session_protocol.py -q`: 35 tests passed.
- `PYTHONPATH=backend python3 -m unittest discover -s tests -p test_keybindings.py -q`: 23 tests passed, including every shipped key's parsing (#Z00M, already fixed by 4d540c0e).
- `python3 docs/qa_evidence/2026-09-21-pane-startup-recovery/drive.py`: isolated Xvfb, disposable HOME/XDG settings, fake worker and no provider calls. Screenshots and request traces accompany the four passing cases: Plan before first prompt, toggle back to Build, automatic recovery with Plan preserved, repeated failure bounded to two processes followed by explicit retry after repair. Every case asserts exactly one ask, expected mode and identical replayed configuration.

#CFG1 already landed in 4764200e. Reviewed `RelayWindow::sendFromConsole`: only an ask changes the console context or reconfigures an existing shared worker. Its original live before/after evidence is `docs/qa_evidence/2026-09-21-console-configure-loop/` (654 configures versus 3); no duplicate fix was made.

Repository-wide board validation reports existing invalid MDL1 card/thread IDs and the existing three-character a10 task marker; these belong to the active model-picker card. This change does not rename its identity or unrelated tasks. #40SN has no card validation errors.
