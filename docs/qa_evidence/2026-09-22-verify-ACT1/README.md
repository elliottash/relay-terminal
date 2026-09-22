# ACT1 verification — 2026-09-22 UTC

PASS. A fresh isolated Xvfb/config profile opened Activity beside a terminal, quit through SIGTERM (Relay’s graceful layout-save handler), and reopened `build/relay` with **no arguments**.

`before-quit.png` and `after-reopen.png` show both panes. `saved-layout.json` and `restored-layout.json` have identical tab trees: a horizontal split containing `pane` and `internals`, the same cwd, scrollback UUID, and Activity owner. The restored terminal visibly includes its saved scrollback.

Reproduce: `bash docs/qa_evidence/2026-09-22-verify-ACT1/drive.sh`.
Targeted `ctest --test-dir build -R '^(windowstate|consolemode|agentcontext)$' --output-on-failure`: 3/3 pass. Build through `scripts/relay-build --wait-seconds 60 --target relay relay-windowstate-tests` passed.

Implementation: `7dd8fdad`. Verified the shared checkout; unrelated uncommitted edits were present and were not changed or committed. No product-code fix required.
