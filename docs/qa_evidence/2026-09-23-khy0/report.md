# KHY0 QA diagnostics

Implementation adds a disposable `scripts/relay-qa-run` wrapper and a safe protocol-exception table to `scripts/relay-events.py`. The wrapper labels child records `qa`, assigns a bounded run ID, and isolates XDG data/config/cache, the keyring, local-model registry, and memory import. `--keep` retains the private profile for inspection. The reporter maps protocol request kinds and exception classes through fixed allowlists; it exports no `msg`, pane, session, run ID, argument, or output field in this table.

Validation on 2026-09-23:

- `scripts/test.sh --junit /tmp/relay-khy0-tests.xml tests.test_event_report` — 8 passed. The subprocess regression exercises a real `relay_core.logs` write under the wrapper and verifies the normal XDG log path remains absent and the child sees a QA origin/run ID.
- Live retained-log summary from `python3 scripts/relay-events.py --since 2026-09-23T12:00:00Z --origin interactive --json`: 136 protocol-error events at scan time, including the two known `configure`/`AttributeError` events; six unknown-kind events remain grouped as `unknown`. This is a live rotating log window, not a fixed incident count.
- `git diff --check` on the scoped files — passed.
