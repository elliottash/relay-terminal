# Queue recall verification

- Built `relay`, `relay-queuenav-tests`, and `relay-queuesubmit-tests` with `scripts/relay-build`.
- `ctest --test-dir build -R '^(queuenav|queuesubmit)$' --output-on-failure`: both passed.
- `python3 docs/qa_evidence/2026-09-21-queue-recall/drive.py`: passed under isolated Xvfb, XDG configuration, and a real shell. No provider requests.
- Two queued shell commands behind `sleep`: Up restores the first verbatim and removes its row. The second executes when sleep finishes; the recalled command does not. Its draft survives queue completion, then executes on Enter.
- Single queued command: recall removes it, editing works, and Enter executes the edited draft.
- Screenshot `recalled.png` inspected: one remaining row, no PAUSED label, recalled command in composer. `resubmitted.png` records completion.
- QueueNav's existing occupied-prompt and multiline tests pass. Steer withdrawal and noneditable worker-preview fallback use existing paths; no live provider test was performed.
- Board check reports pre-existing errors in MDL1 task markers/thread and A9QR thread timestamps; no QRC1 findings. Relay board/test MCP tools are not exposed in this turn.

- Final verification: exported commit `3ebf3673` to `/tmp/relay-qrc1-3ebf3673`, built using its `scripts/relay-build`, and reran `drive.py` against that binary: PASS. Screenshots were refreshed from this run. An intervening run was invalidated by deleting its temporary build directory during execution; the stable export resolves that test setup error.
- Called `TestsCommands.check_card("QRC1")`, the implementation behind `tests_check`: no findings, failures, or blocking signals.
