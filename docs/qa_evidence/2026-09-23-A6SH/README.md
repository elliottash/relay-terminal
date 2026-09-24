# SSH defaults to agent control

`drive.py` ran Relay's libvterm build in an isolated Xvfb and XDG profile. It logged in through real OpenSSH to localhost, using a deterministic fixture worker that supplied an agent configuration without provider calls.

The drive passed: the first remote prompt granted program control and showed **Agent driving ssh** with **Take over (Ctrl+H)**; Ctrl+H sent `program_state` with `granted: false` and `reason: take_over`; returning to the prompt box did not grant control again. `events.jsonl` contains only remote-session and program-state messages. `01-agent-driving-ssh.png`, `02-takeover.png`, and `03-prompt-box-restored.png` show the corresponding pane states.

Build: `RELAY_JOBS=2 scripts/relay-build --fast --target relay` in `/tmp/relay-a6sh` (clean committed baseline plus the Pane change), passed. The shared checkout build was blocked by another session's concurrent, uncommitted `ModelCatalog.cpp` edit, so the isolated binary was used for the drive.

Run: `RELAY_SSH_TEST_BINARY=/tmp/relay-a6sh/build-fast/relay python3 docs/qa_evidence/2026-09-23-A6SH/drive.py` — passed.

This fixture exercised localhost, not a separate remote machine. The password revocation and per-program human-control paths were inspected in `Pane::beginDelegation`, `Pane::checkPasswordPrompt`, and `Pane::controlFor`; this drive did not exercise them live.
