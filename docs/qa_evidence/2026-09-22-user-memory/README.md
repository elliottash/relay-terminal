# User memory and interview — implementer evidence

Card #M7RY, 2026-09-22. This is implementation evidence, not independent QA.

## Results

- `PYTHONPATH=backend:tests python3 -m unittest -q test_user_memory_tools test_globals_protocol test_memories test_app_tools test_agent_context`: 125 tests passed. The requested pytest invocation was unavailable because this interpreter has no pytest; the existing suites use unittest.
- `ctest --test-dir build -R '^globalspane$' --output-on-failure`: passed. Covers user-only filtering, summary search, interview callback, source editing, drafts, stale replies and conflicts.
- `scripts/relay-build --target relay-globalspane-tests relay -j 4`: application and widget tests built. Subsequent `scripts/relay-build --target relay -j 4` passed after the first-click fix (build stamp 2026-09-22.19H.06). Existing ForkText initializer warning is unrelated.
- `python3 docs/qa_evidence/2026-09-22-user-memory/drive.py`: passed in Xvfb with isolated HOME/XDG/global memory and a scripted localhost provider. It opens Globals, clicks Interview me, checks that the Globals interview brief reaches the model, executes the real list/save tools, saves an explicitly requested preference, refreshes and selects it in the editor. See `result.json` and screenshots.
- `git diff --check`: passed.
- `python3 scripts/relay-board.py check --json`: no diagnostics for this card; checkout has 12 errors and 754 warnings in unrelated cards/threads at check time. No unrelated board records changed.
- `TestsCommands('.', 'issues').check_card('M7RY')`: test references resolve except the newly added, not-yet-tracked test file is called retired before landing; test history has no registered run for app_tools. Direct command results above establish execution. Recheck discovery after landing.

## Screenshots

1. `01-user-memory.png`: default User memory section and interview entry point.
2. `02-interview.png`: first interview question in the existing Globals helper.
3. `03-saved.png`: saved preference visible in the list and editable source.

The first live attempt exposed a first-click submission before console attachment. Submission now runs on the next event-loop turn, after the existing attachment callback. The repeated drive passed without a second click. The driver also skips first-launch approvals only within its isolated test settings, to keep viewport coordinates repeatable.

## Limits

The scripted provider tests plumbing, not whether a production model chooses good interview questions or follows confirmation guidance in every case. That needs independent behavioral review. Facts remain Markdown cards; retirement excludes future memory context but retains source/history and does not erase old conversations. Refresh displays helper edits. No new background learning, purge or global memory-use switch is implemented. Research distinguishes these future recommendations from delivered behavior.

Full research: [Relay user memory design](../../../reports/Relay%20user%20memory%20design.md).
