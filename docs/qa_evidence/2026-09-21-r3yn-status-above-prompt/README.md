# #R3YN implementer evidence

## Live helper-console check

`live-waiting/helper-waiting.png` is a real `relay-console-harness` under Xvfb with
`RELAY_CONSOLE_STATUS=waiting`. The fixture injects one background subagent through the same
worker-event path a Board, Options, or Sessions console receives.

The frame shows:

- neutral `Relaying · waiting for 1 subagent . . .` status on the pane background;
- the status is outside and immediately above the rounded composer frame;
- `Ask about this board…` remains inside the editor as its ordinary context placeholder;
- action buttons, chips, subagent rows, and transcript retain their established layout.

`live-waiting/helper-waiting.txt` is OCR from that frame and contains both `Relaying` and
`Ask about this board`. `live-waiting/harness.log` is empty: the harness emitted no warning or
crash. The status-line timer and `pulseScale()` path are covered by `consolemode` and
`panestatus`/`pulsepaint`, respectively.

## Automated checks

- `ctest --test-dir build -R '^(consolemode|panestatus|pulsepaint)$' --output-on-failure` — 3/3 passed.
- `scripts/relay-build --target relay` — passed.
- `scripts/relay-build --target relay-console-harness` — passed before the live capture.

The first live attempt exposed a sibling-coordinate crash in `PaneBusyLine::leftInset()`. It was
fixed by mapping both widgets through global coordinates; the successful frame above is from the
rebuilt harness after that fix.
