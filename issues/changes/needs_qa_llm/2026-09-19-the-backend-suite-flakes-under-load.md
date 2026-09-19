---
id: 99T0
type: work
status: needs-qa-llm
labels: [bug, tests, flake]
component: [worker, agent]
workstream: agent
assignee: agent
implemented_by: anthropic/claude-fable-5.1
rank: m
created: '2026-09-19'
acceptance: the four named tests pass under eight concurrent copies of the suite, and `backend-and-bash` finishes inside its 600 s ceiling
source: 'owner decision A2, 2026-09-19: the backend test flakes'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# The backend suite flakes under load, and one flake can hang ctest for 600 s

## Issue
Under load, tests/test_queue.py (WorkerQueueProtocolTests.test_failed_turn_pauses_queue, a
SteerTests case test_remove_refuses_a_steer_already_delivered,
SupervisorTests.test_queue_runs_in_order_without_overlap) and tests/test_jobs.py
(JobToolTests.test_shutdown_stops_every_job) stall or fail intermittently; alone they pass. With
several suites running at once the ctest `backend-and-bash` target then hits its 600 s ceiling.

## Decisions
- 2026-09-19, owner (decision A2): investigate for real. For each failure decide whether it is a
  test that assumes timing — fix the test, waiting on the event and not on a sleep — or a real
  ordering bug in the shutdown/queue code, in which case fix the code with a regression test.

## Findings
**None of the four was a bug in the queue, the supervisor or the jobs code.** All four were tests
that assumed a timing, and the fix in each is to wait on the event the code already signals. What
the investigation did find in the code is one test that could not fail, and one test that could hang
the whole target — the reason a single flake costs 600 s.

1. `test_queue.SupervisorTests.test_queue_runs_in_order_without_overlap` — three prompts are queued
   and the `queued` events are expected to report positions 0, 1, 2. The dispatcher thread takes the
   head of the queue as soon as it is there, so on a loaded machine `first` was already dequeued
   when `second` was submitted and the positions came out 0, 0, 1. Fixed by waiting for the head's
   `agent_started` before submitting the other two; the positions asserted are then 0, 0, 1, which
   is the queue's own ordering rather than a race with the dispatcher.
2. `test_queue.SteerTests.test_remove_refuses_a_steer_already_delivered` (and the three sibling
   cases that shared the pattern) — the running turn's tool was `sleep 1`, and the steering prompt
   had to reach the supervisor inside that second: after the turn's next step boundary the turn
   never takes it and reports `steer_returned` instead of `steer_delivered`, so the test waited five
   seconds for an event that was not coming. `SlowToolThenAnswer` now runs a command that waits for
   a file, and each test opens that gate after submitting its steer. No window to miss.
3. `test_queue.WorkerQueueProtocolTests.test_failed_turn_pauses_queue` — **this is the one that
   hangs ctest.** A local server closed each connection after 0.5 s so the turn would fail, on the
   assumption that the second prompt was queued by then. Under load it was not: the first turn
   failed with an empty queue, so nothing paused — which is correct, "the pause resets when the
   queue empties" — the second prompt then ran, and the `queue_changed {paused}` the test reads for
   never arrived. The read loop checked its 10 s deadline only *between* blocking `readline()`
   calls, so it sat forever on a worker that was itself waiting on stdin. The test never returned
   and took `backend-and-bash` to its 600 s ceiling with it. Fixed on both counts: the server now
   holds the connection until the test has seen the second prompt queued (so the failure is ordered
   against it), and a watchdog kills the worker after 60 s so a broken run fails this one test
   instead of hanging the suite.
4. `test_jobs.JobToolTests.test_shutdown_stops_every_job` — this test **could not fail**.
   `ToolExecutor.shutdown()` is `stop_all(forget=True)`, and forgetting empties the table, so
   `jobs.running() == []` held whether or not anything had been stopped. It now keeps the two jobs,
   waits on each one's `done` event and checks the processes are gone — which is what the name
   claims and what a shutdown ordering bug would break.

Three more load flakes turned up in the same reproduction and are fixed the same way, by waiting on
an event instead of a clock:

5. `test_jobs.JobToolTests.test_the_live_stream_runs_only_while_a_call_waits` — the worst of the lot
   at 9 failures in 16 runs. `echo a; sleep 1.2; echo b` against a one-second timeout: "b" landed
   0.2 s after the call stopped waiting, and "a" had to be printed by a freshly spawned bash inside
   that same second. Split into two jobs, each ordered on an event. The interesting half is the
   first: `JobTable.wait` installs the live callback only on a job that is *still running*, so an
   `echo` that finished before the call reached `wait` is not streamed at all — which is the
   documented behaviour, and the reason the first half cannot be written with a sleep either. The
   test now opens the command's gate from a watcher that waits for `job.live` to be installed.
6. `test_jobs.JobListTests.test_a_handed_back_job_is_listed_and_its_end_announced` — `sleep 1;
   exit 2` against a one-second timeout: when the wait overran the sleep the job was already
   finished, so it was never handed back, nothing was listed, and `lists()[-1]` raised IndexError.
   Gated the same way.
7. `test_session_protocol.GuestSessionRows.test_a_rescan_is_not_queued_per_keystroke` — 9 in 16.
   The test types three queries and asserts the rescan's second answer is built for the last one; on
   a loaded machine the reconcile finished before the third query was typed and the answer came back
   for `drag`. The mocked `guest_sessions.reconcile` now holds its thread until all three listings
   are in, which is the ordering the test is about.
8. `test_ssh_shell.RemoteScriptTests.test_inside_a_real_tmux` — 7 in 16. `time.sleep(0.5)` between
   typing the shell-integration bootstrap and typing the next command: under load the eval was still
   running, the next line went into it, the command never ran, and the pane held no `133;C` mark.
   It now waits for the first *marked* prompt to reach the tmux pane, draining the pty while it
   does.

Two more surfaced once the loud ones were quiet, and are fixed here as well:

9. `test_session_protocol.GuestSessionRows.test_rename_and_pin_stay_in_the_index_and_survive_a_rescan`
   — 3 in 48. The throttle is 0 in that class, so every listing sets a background reconcile going,
   and one of them sometimes refreshed the touched transcript before the test's own `reconcile()`
   call, which then counted `refreshed: 0`. The throttle goes up and any in-flight pass is waited
   out before the mtime is touched.
10. `test_queue.WorkerQueueProtocolTests.test_failed_turn_pauses_queue`, the other assumption in it —
    1 in 48 — `events[-1]['items']` takes the worker's very last line to be the `queue_changed` from
    `queue_clear`. It is not always, and a `KeyError: 'items'` is what that looks like. It reads the
    last `queue_changed` now.

## Left for the owner
- **`relay_core/conv_index.py` shares one sqlite connection between threads with no lock** — a real
  bug, found here but not this card's to fix. One run in 48 died with `sqlite3.InterfaceError: bad
  parameter or other API misuse` inside `ConversationIndex.search` (`conv_index.py:1732`, via
  `session_protocol._conversations_event`). The connection is opened `check_same_thread=False`
  (`conv_index.py:754`, `:779`) and the module holds no mutex, while `session_protocol._conversations`
  runs `guest_sessions.reconcile(self.index())` on its own thread against the same connection as the
  listing that scheduled it. `check_same_thread=False` turns off the check, not the requirement. The
  fix is a lock around every use of `self._db`, not only `_run` — a change to a central module in the
  guest-sessions work (GT7X) that deserves its own card.
- **Six `test_ssh_shell.WrapperTests` cases fail in any isolated `HOME`** — 16 of 16 runs, with or
  without CPU contention, and they pass with the developer's own `HOME`. They read the owner's
  `~/.ssh` configuration. `backend-and-bash` isolates `XDG_CONFIG_HOME`, `XDG_DATA_HOME` and the
  keyring but not `HOME`, so it does not see this; a CI machine or another user would. Whether those
  cases should build the ssh configuration they need or be skipped without one is the ssh work's
  call, not this card's.
- **`test_web_viewport.ViewportTests` can take the interpreter down with it.** In one eight-copy run
  all eight died inside `test_the_real_client_fits_portrait_and_a_phone_with_the_keyboard_up` with
  no traceback and no summary — eight concurrent Chromes under twenty busy loops. That is the web
  client's area.
- **`test_subagents.test_cancelled_main_turn_does_not_wake_and_keeps_result`** failed 3 of 16 runs.
  It is in `agent.py`/subagent territory that another session is working in, so it is named here
  rather than changed.

## Tasks
- [x] Reproduce under load <!-- t:5c -->
- [x] Decide test-or-code for each failure <!-- t:f2 -->
- [x] Land the fixes with the reproduction rate before and after <!-- t:cw -->

## QA checklist
- [x] Reproduced before the fix: eight concurrent copies plus twenty busy loops on a pristine
      `git archive refs/heads/main` export. Whole suite, 16 runs (`-p 'test_[a-u]*.py'`, to keep
      eight concurrent Chromes out of it): live-stream 9, rescan 9, real-tmux 7, subagent-cancel 3,
      keybindings 2; and in an earlier whole-suite run, `test_failed_turn_pauses_queue` 2 of 8 with
      3 of 8 copies not finishing inside 700 s at all.
- [x] Focused on the four modules, same load, 48 runs before → after: live-stream 29 → 0,
      rescan 13 → 0, handed-back-job 5 → 0, real-tmux 18 → 0 (the thread has both tables). A final
      64-run pass over the four modules with every fix in: nothing failed but the six
      isolated-`HOME` ssh cases below, which are not load flakes.
- [x] Whole suite after the fix, 16 runs: every test changed here at 0, and no copy failed to
      finish.
- [x] `python3 -m unittest tests.test_queue tests.test_jobs tests.test_session_protocol
      tests.test_ssh_shell` on the exact would-be tree: all pass.
- [x] Every change waits on an event the code already signals — a queue event, a `done` event, a
      file the test creates, the live callback being installed — and no changed test sleeps for a
      fixed time to let something happen.
- [ ] A QA session confirms the four named tests, and the three found alongside them, still pass on
      an idle machine and that no assertion was weakened to make them pass: each one asserts the
      same fact as before, and `test_shutdown_stops_every_job` asserts more than it could before.
