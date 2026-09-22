# boardexecute fails at pristine HEAD (card #3BPH)

The test `tests/boardexecute_test.cpp` was committed at 70c5be22/#48S3 but was never wired into
CMakeLists.txt, so it never ran. Wiring it (the uncommitted CMakeLists.txt hunk) and running it:

1. In the working tree: `ctest --test-dir build -R boardexecute` → 3 of 3 cases fail
   (`'button' returned FALSE` at lines 119/161/198 — no `boardExecute`/`boardReplyButton`
   button exists on the open card page).
2. In a clean worktree at HEAD (4e6e483b) with only the CMakeLists.txt hunk applied:
   `cmake -S . -B build && cmake --build build --target relay-boardexecute-tests` then
   `ctest --test-dir build -R boardexecute` → identical 3 failures.

So the failure is in committed Board code (or the committed test), not in any uncommitted edit.
Findings during /clean-commit, 2026-09-22, pane a32ee1d3. Worktree removed after the check.
