# #XCXD evidence — independent agent/terminal queues, stable repeated Enter, Esc/Alt+Esc

Captured 2026-09-24 from the current source tree (offscreen Qt), by the queue-contract suite itself:

```
scripts/relay-build --target relay-consolemode-tests
RELAY_XCXD_EVIDENCE_DIR=$PWD/docs/qa_evidence/2026-09-24-XCXD \
  QT_QPA_PLATFORM=offscreen ./build/relay-consolemode-tests --xcxd-only
```

| Shot | State |
|---|---|
| `queue-wide.png` | Agent turn and `sleep 20` both running; two agent prompts and one shell command queued. Agent and Terminal lanes side by side, each with its own count, paused state and Resume. Only **Stop shell (Alt+Esc)** in the header: the agent's stop is the Relaying line's "Esc stops". |
| `queue-narrow.png` | Same state at 480 px: the two lanes stack. |
| `queue-shell-only.png` | Only the shell runs, nothing queued: the strip shows the running command and **Stop shell (Esc)**, and no empty "Terminal · 0" lane. |

Owner feedback applied on top of the implementation (2026-09-24, screenshot of a busy agent with nothing queued):
"remove that extra terminal line, and the stop agent (esc) is redundant with the relaying line."

- A busy agent alone no longer raises the queue strip; the strip never draws a lane header for an empty lane.
- The `Stop agent (…)` button is gone; `Stop shell (Esc | Alt+Esc)` stays, because nothing else offers it.
- Regression: `xcxdUiCases` checks a busy agent with nothing queued shows no strip, no lane label and no Stop agent,
  and that a running shell with nothing queued shows no lane label.

Tests (current source, 2026-09-24):

- `ctest --test-dir build -R '^(consolemode|queuecontract|queuenav|queuesubmit)$'`: 4/4 passed.
- `PYTHONPATH=backend python3 -m unittest tests.test_terminal_context tests.test_terminal_context_integration tests.test_queue`:
  88 run, 87 passed. The failure is `ConsoleFieldTests.test_a_card_turn_is_refused_the_writers_at_call_time_and_told_why`
  (expects "Execute", message says "Run"), already recorded on #E728 and not touched by this card.

Not synthesized: drag reordering inside a lane needs a real drag source; lane isolation is covered by the × and Clear cases.
