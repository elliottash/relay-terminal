# W1B8 verification evidence

- `python3 -m unittest tests.test_verify_runner_source`: 3 passed.
- `git diff --check -- src/Pane.h`: passed.
- `scripts/land.py commit w1b8-run ...`: the exact tree for commit `645b9fdf` built the `relay` target successfully in isolation.
- `scripts/land.py commit w1b8-chip ...`: the exact tree for commit `f13bf691` built the `relay` target successfully in isolation. The pane keeps `#ID` in its header while a Board task waits for guest startup or the agent queue, then transfers it to the running turn.
- `scripts/relay-build --target relay`: stopped at unrelated shared-checkout edits. `src/RelayWindow.h` calls missing `Pane::agentReady`; `src/WindowManagerImpl.h` defines `WindowManager::refreshBackgroundTasks` and `backgroundCount` without matching declarations. The changed `Pane.h` lines emitted no error before compilation stopped.

After the concurrent background-task work compiles, build and run Relay with an isolated profile. Put a guest harness at rank 1 of the Main model list; choose Run on a Board card. Check that the guest begins the card turn without typing into the pane, its `#ID` stays beside the pane title through startup and while the turn runs, clicking it opens the card, and a plain newly opened guest pane remains idle until its first prompt.
