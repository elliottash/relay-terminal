# Alt+A closes the active subagent pane (#A7CP)

Implementation checks, 2026-09-22:

- `scripts/relay-build --target relay relay-subagents-tests`: passed.
- `ctest --test-dir build -R '^subagents$' --output-on-failure`: 1/1 passed.
- `python3 docs/qa_evidence/2026-09-22-alt-a-close/check.py`: passed under Xvfb with isolated XDG config, data and runtime directories.
  - Restore a main pane and its saved subagent transcript; focus the transcript and send an actual Alt+A keypress. The subagent pane disappears from the persisted layout.
  - Ctrl+Shift+Z restores the closed view.
  - Focus the main pane and send Alt+A: the view stays open and receives focus. A second Alt+A closes it.
- Code inspection: the close path removes the view, with no stop/cancel request to its owner's worker; the copied owner callback restores prompt focus after closing.
- `scripts/relay-board.py check`: existing tracker errors elsewhere (12 errors); no finding names this card or its thread.

This uses a saved transcript, not a running model. Independent verification remains pending.
