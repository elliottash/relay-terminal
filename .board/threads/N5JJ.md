<!-- relay:entry 20260920T180002Z-c3 author=claude-code kind=event -->
### Claude Code · 2026-09-20 18:00
created this card in Planned from the #PF4K profile: measured findings and the proposed fix; evidence in docs/qa_evidence/2026-09-20-perf-profile/


<!-- relay:entry 20260920T210000Z-n1 author=claude-code kind=progress -->
### Claude Code · 2026-09-20 21:00
Landed all three parts of the orchestrator's decision, in that order of importance: Relay's own thread appends now replace the file instead of appending in place (`board.append_to_thread`, with the lock moved to the threads directory because `os.replace` changes the inode), the pane keeps a file watch on the open card, its thread and the `in-progress` cards within a 32-file budget, and a debounced `board_refresh` runs on show and focus-in for the rest. No polling timer (#057J).

60 writes at one a second: Relay's own writers **40 of 60 → 60 of 60**; a foreign in-place writer on the card the pane is working on **0 of 60 → 40 of 60** (the 20 still missed are `BOARD.md` touches, a generated index). Harness and numbers: docs/qa_evidence/2026-09-20-perf-fixes/board/WATCHER.md.

Tests: `tests/test_board.py::ThreadTests` (a new inode per append; four concurrent writers keep all 24 entries) and a new `tests/boardwatch_test.cpp` / `boardwatch` target, whose negative control — the same card in `inbox`, so it gets no watch of its own — fails on the timeout, which is this card's bug.
