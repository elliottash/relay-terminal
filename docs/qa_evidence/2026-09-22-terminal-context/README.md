# #TCXT terminal command evidence — implementer's evidence

Implementation landed in 279fe679, 096285c1, 274b87e4, fea4f6ca, 310a96a7, bdf93cc4, a4847c3d
(components, hooks, review fixes), 567d9f18 (Pane/agent/guest-bridge/protocol integration),
aaa8d03f (test-harness start directory) and 88c69ffb (live sync only while a turn is in flight).

## Live GUI drive

`live-drive.py` starts the real `relay` binary under Xvfb with isolated XDG/TMPDIR, the index's
terminal history and output switched off, and a deterministic fixture worker (no external model)
that records every request the pane sends and echoes the terminal snapshot it received.

```
RELAY_TEST_BINARY=<binary> python3 docs/qa_evidence/2026-09-22-terminal-context/live-drive.py
```

Last run on the binary `scripts/land.py` built from the exact tree of 88c69ffb
(`/tmp/claude-1000/land/tcxt-claude/verify/build/relay`): PASS, all 11 checks true in `results.json`,
namely composer capture, exit status, head/tail markers, history disabled, no model request on
completion, snapshot in `ask`, removal, manual, explicit attachment, off, and native Bash capture.
`terminal-context.png` is the final screen, showing the chip, pinned snapshots, and
"No terminal evidence attached." for the removed/off turns.

## Targeted tests

```
PYTHONPATH=backend:.:tests python3 -m unittest tests.test_terminal_context \
  tests.test_terminal_context_integration tests.test_guest_board_bridge \
  tests.test_prompt_profiles tests.test_terminal_command_hooks        # 72 tests, OK on a clean export
scripts/relay-build --target relay-terminalrecords-tests && ctest --test-dir build -R terminalrecords
```

## Not exercised live here

Two shell panes side by side and an SSH login were not driven in the GUI. Pane/generation
isolation and the remote branch are covered by `test_terminal_context_integration` and
`terminalrecords_test.cpp` only. Those scenarios are left for the independent verifier.
