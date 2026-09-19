# QA evidence — Up/Down walks this pane's own history (#H7N4)

Date: 2026-09-19. Change: one prompt-history file per pane, keyed by the pane's layout id,
replacing the single shared `state/prompt-history.txt`.

## What was run

- `make -f CMakeFiles/Makefile2 relay relay-prompthistory-tests relay-editor-tests` in `build/` —
  the whole app and both test targets compile and link. (`cmake --build build` could not be used:
  the working tree's `CMakeLists.txt` references `tests/paneusage_test.cpp`, which does not exist
  yet — in-flight work from the pane-usage card, not this change. `ctest.log` here.)
- `ctest -R "prompthistory|editor"` — 2/2 pass (`ctest.log`). New cases: per-pane paths keyed by
  pane id, prune keeps the ids that can come back, `clearDirectory`, two boxes on two files never
  see each other's lines, `forgetAllHistory` empties a box mid-browse.
- Full `ctest` (2341 tests, same tree): every C++ group passes. Four failures in
  `backend-and-bash` — `test_remote_wire`, `test_roles` (2), `test_sessions` — all in Python
  modules with uncommitted edits from other in-flight work (`backend/relay_core/roles.py`,
  `sessions.py`, `router.py`); this change touches no Python.

## Not covered here

A live GUI run: type in two panes, press Up in each, quit and reopen Relay, and check each pane
recalls only its own lines. The wiring (`Pane::promptHistoryPath()`, `initRestore` re-pointing the
composer at the restored id) is exercised only by the editor-level tests, not by a real pane.
