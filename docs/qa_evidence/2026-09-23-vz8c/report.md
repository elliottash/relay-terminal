# VZ8C consolemode recovery

The recorded signal opened on two `ctest:consolemode` failures at commit `907357d5`. In a detached worktree at `da47dcbe`, the test target initially failed to link because the `relay-consolemode-tests` and `relay-console-harness` targets omitted the five `Pane*.cpp` files split out of `Pane.h`. Adding those source files to `CMakeLists.txt` made the test binary build.

The first full run then failed in `theContextGetsFirstRefusalOnEveryLinkKind`: session links now bypass the local context and request `conversation_open` from the worker. The updated test sends the worker's `ready` event and checks that behavior. The repeated-Enter test also expects the `terminal_context_update` emitted before a steered `ask`. No queued-prompt production behavior changed.

At the isolated tree (commit `da47dcbe` plus only `CMakeLists.txt` and `tests/consolemode_test.cpp` changes):

- `scripts/relay-build --target relay-consolemode-tests` — passed.
- `scripts/relay-qa-run ctest --test-dir build -R '^consolemode$' --output-on-failure` — passed twice, 2.43 s and 2.45 s.

The Board runner also recorded consecutive passes for `ctest:consolemode` in runs
`20260923T232058Z-8e4b` (2.43 s) and `20260923T232337Z-894a` (2.37 s). The
signal resolved at 2026-09-23 23:23:39 UTC; `board_signals list` reported no
open signals. These recorded runs used the shared checkout's built test target,
while the detached worktree above isolates the source changes under review.
