---
id: VK6J
type: work
status: planned
labels: [feature, tests, performance, landing]
parent: 3MH4
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-26'
source: Claude pane 21c53ef4, 2026-09-26
links: {plans: [], commits: [], evidence: [], related: [Y2PQ, EHBH, D9AQ], github: null}
---
# Make the queue gate fast: run the Python suite in parallel and retire stale, broken and redundant tests

## Issue
Every code landing waits on a full gate, and the gate is about 15 minutes, almost all of it the Python suite running serially. The owner wants it fast enough to wait for, and stale or redundant tests removed. Measured on gate 18ec5ef0 (2026-09-26): ctest 34 s in total; the Python suite (223 test files, ~4,000 tests, one process) ~12 min on a loaded 20-CPU host, ~2.5 min when idle; 124 known failures (111 Python + 13 ctest), 21 of which now pass.

> skip thee gates again, i cant wait that long, and if you can already tell how to speed that up or remove stale / redundant tests, i'd like you to start a card on that.
> — elliott · [session:97d268b4846648f49e6aba30a5ebe433](relay://session/97d268b4846648f49e6aba30a5ebe433) · 2026-09-26

## Plan
Target: a code gate under 5 minutes on an idle host, and no test that is known to be broken or redundant left in the suite.

1. **Parallel Python suite (largest win).** `scripts/gate-known-failures.py` runs `unittest discover` in one process. Split the 223 `tests/test_*.py` modules across N worker processes (N = min(cpus, 12)), each with its own `XDG_DATA_HOME`/`TMPDIR`, balanced by recorded per-module duration (longest first), and merge the results before judging against `.relay/known-failures.txt`. pytest-xdist is not installed; a small stdlib sharding runner avoids a new dependency. Modules that share global state (tmux sockets, fixed ports, Xvfb displays) get marked serial and run in one extra shard. Expected: ~12 min to ~1-2 min.
2. **Measure first.** Run the suite once with `relay_core.junit_runner` to get per-test durations; list the slowest 30 tests and every `time.sleep` over 1 s in `tests/`. Most slow tests should poll with a deadline, not sleep.
3. **Shrink the known-failure lists.** 21 of the 111 Python entries passed on gate 18ec5ef0: delete them (the gate already reports them). For the remaining ~90 Python and 13 ctest failures, triage each: fix when the test is right and the code regressed; update when the test pins text or behaviour the owner changed deliberately (e.g. settings expects "Helper Agent", calllines expects no timestamp); delete when it tests something retired (HelperChat-era suites, board_chat). Existing cards #Y2PQ, #EHBH and #D9AQ cover some of these.
4. **Remove redundancy.** `backend-and-bash` in ctest duplicated the whole Python suite and is already skipped in the gate; remove it from CMakeLists (or make it a label excluded by default). Find tests that run the same scenario at two levels (e.g. landq unit tests and the parallel-landing acceptance flow) and keep the cheaper one where they assert the same thing.
5. **Fix flaky tests or quarantine them with a reason.** `queuecontract` fails 2 of 6 runs on main: it types `sleep 2` 100 ms after building a pane and waits 5 s. Wait for the shell's first prompt mark instead.
6. **Selective gating (later).** Once the suite is fast, consider running only the modules affected by a candidate's paths for docs-only or backend-only changes, with a full run on a schedule.

Verify: gate wall time on an idle host before and after; known-failure list sizes before and after; three consecutive gates with no flaky failure.
