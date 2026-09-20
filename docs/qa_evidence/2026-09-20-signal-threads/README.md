# Signal threads: a failing test nobody is on works itself (#AQ6X phase 3, 2026-09-20)

Card: `issues/features/2026-09-20-signals-a-card-type-for-machine-written-faults-s.md`, decision 9
and plan step 7. Wire: `docs/AGENT-SESSIONS-PROTOCOL.md` §32.10. Design:
`docs/SWITCHBOARD-DESIGN.md` §4.11.2b.

The owner's words are the whole specification: *"6 -- i think yes by default, but its optional"*, and
*"you get a notification that you can click on to open the agent thread, and those go into the
sessions manger"*.

## The loop, live

`loop.py` builds a throwaway project with **one real failing test** and runs the real machinery over
it: a real board (`signals: {auto_work: true}`), a real run history, a real `SubagentManager` +
`SubagentFactory` on the `signal` definition, a real `Agent` with the real file and command tools, a
real `TestsCommands` folding and picking up exactly as the board worker does (the spawn it is handed
is the body of `board_protocol._spawn_signal_thread`), and a real test run through
`relay_core.junit_runner`. The key is a `unittest:` one because that is the runner the worker can
drive in a plain directory, with no cmake and no build tree — which is what makes the *worker's own*
run of the check part of the run rather than a skipped branch.

Two modes, both of which ran:

| file | the thread's model | what it proves |
|---|---|---|
| `loop-local-model.txt` | **`bonsai-2-27b`, served on this machine** (`127.0.0.1:8080`, no key — memory/local-llm-setup) | a real model, given the task text and nothing else, reads the fault and fixes it |
| `loop-stub.txt` | a scripted provider: four steps through the real Agent and the real tools (look, fix, run the test, report) | the same loop, deterministically and in two seconds |

`loop.py --stub` is the second. The scripted provider replaces the *model* and nothing else; it is
there because the model on this host is a reasoning model at about half a token a second, so a live
agentic fix takes four or five minutes. Re-run either with
`docs/qa_evidence/2026-09-20-signal-threads/loop.py [--stub] [seconds]`.

### What the runs show, in order

1. **Fold 1: the signal opens and nothing happens.** Two failing executions of the key make it
   `broken`, `open`, unclaimed. `threads running: 0` — the pane whose run opened it has this fold to
   claim it (step 7a: `tests_run`'s result tells it so in the same turn).
2. **Fold 2: Relay picks it up.** One `signal_thread {state: "started", key, thread_id, session_id}`,
   and the signal log gains `{"action": "claim", "session": "<thread id>"}` — the claim is **under
   the thread's own id**, which is what makes the board's chip name the thread.
3. **The task it was started with** is printed in full: the key, the failure in the check's own
   words, the three ways the run may end, and this checkout's rules (`scripts/land.py`, claim late,
   dry-run, `who` before touching a file, `scripts/relay-build`). It reaches the board through
   `scripts/relay-board.py signals`, because a subagent has `run_command` and never `board_*`.
4. **The agent works and stops.** With the local model: `width.txt is now '3'` after 214 s, from a
   model that was told only what is in that task text, whose own last words are *"Two consecutive
   passes confirmed (`OK`, exit 0 both times)"*. With the scripted provider: the same in 2 s.
5. **The worker finishes the thread by itself** — nothing in the script asks it to; the watcher
   `_spawn_signal_thread` armed on the subagent's `done` event does it — and it **runs the check**
   twice. The history gains two `pass` rows under two new run ids, the signal log two `run` lines,
   `green_streak` reaches 2, the signal is `resolved`, and the event is
   `signal_thread {state: "finished", outcome: "fixed"}`. No card is written and the bugs tab is
   empty, because nothing needed a person.
6. **The thread file** — what the Sessions manager and the ⓘ view read — carries
   `{"type": "signal", "signal": "<key>", "title": "<key>", "description": "<key>"}`.

### The bug this run found

`loop-first-run-found-the-bug.txt` is the **first** live run, before the fix, and it is kept because
it is the reason the fix exists. The local model really fixed the test (`width.txt is now '3'`,
`./check.sh exits 0`) and the outcome was still **`gave-up`**, with a bug card written for a fault
that no longer existed.

Why: a signal thread is a subagent with `run_command`, so the `ctest` it ran was a subprocess in its
own shell and landed in no store. When the watcher fired, the fold had seen nothing since the failure
that opened the signal, so the signal was still `open` — and a thread that ends with the check still
open has given up, which promotes. Every thread would have "given up", always.

The fix is `TestsCommands.verify_signal`: when a thread stops with its signal still open, the
**worker** runs that one key `RESOLVE_PASSES[kind]` times (twice for `broken`), recorded like any
other run, stopping early on a failure — running a broken test again proves nothing. A key this
project cannot run from here is left exactly as it was: a verdict from a check that did not run is not
a verdict. `tests/test_signal_threads.py` has both branches
(`test_the_worker_runs_the_check_itself_before_it_judges`,
`test_a_check_that_fails_again_stops_after_one_run`).

### Two differences from a worker, stated

- **`session_id` is empty** in these runs. It is the owner session the thread file is saved beside,
  and it comes from the main agent `subagents.attach()` gives the manager — which `backend/worker.py`
  does and this driver does not, having no pane agent. In a worker it is the board worker's own
  session, and the thread file is written there; here `thread_data()` is printed instead, which is
  the same dict.
- The **`gave-up` path's card** is proved in `loop-first-run-found-the-bug.txt` (card `ZCMF`) rather
  than in the two final runs, where the check passes and no card is due; the unit tests cover it both
  ways (`test_a_thread_that_ends_with_the_check_still_red_promotes_and_says_gave_up`,
  `test_a_check_that_fails_again_stops_after_one_run`).

## The GUI half

Proved by the Qt tests, which drive the real widgets offscreen rather than the whole app:

| test | what it holds |
|---|---|
| `ctest -R boardsignals` · `aPickupPostsOneNoticeAmendsItAndTheChipReadsLiveWhileItRuns` | a fake `signal_thread started` posts **one** notification — "Working on ctest:panelayout", kind `info`, with an **Open thread** button whose `actionId` carries the thread and its owner; the chip on the signal reads **live** where `paneExists` says no; the chip's activation hands the thread and its owner to `onOpenThread`; and `finished` **amends that same entry** to "Gave up on ctest:panelayout — promoted to #K7Q2" as a `warning` while the chip goes back to closed |
| `ctest -R boardsignals` · the four state cases | live from `started` to `finished`, re-seeded from `signals_changed`'s `threads` list for a pane that opened later, never true for an unknown token, not resurrected by a stale list; every notification wording; the `actionId` round trip |
| `ctest -R conversations` · `signalThreadsAreListedUnderTheProjectWithoutTheBox` | the request always asks for threads; with "Subagent threads" **unticked** the signal thread is the only row, under its project, reading `⚑ signal · ctest:panelayout`, its tooltip saying Relay started it; Enter opens its history rather than resuming; ticking the box adds the user's own thread under its owner without moving it |
| `ctest -R settings` · `workSignalsUnaskedSitsUnderSwitchboardAndSaysWhatItDoes` | Options › Agent › Switchboard carries "Work signals unasked" with the card's explanation, and it sends `signals_config` |
| `tests/test_signal_threads.py` (30 cases) | the fold of grace, the cap of three, `auto_work` and `autonomy: off`, one thread per key, the 24-hour cool-off after a give-up and the regression that overrides it, the claim, the task text, the outcome table, the `signals_config` round trip into `board.yaml`, and the two `verify_signal` branches |

## Not proved live

**No whole-app Xvfb screenshot run.** The notification, the Sessions row and the chip are proved by
the Qt tests above, which build the real `BoardView`, the real `NotificationCenter` and the real
`SessionManager` and drive them with the real events — but not by a run of `build/relay` with a live
thread behind it. Doing that needs the *GUI's* board worker to hold a usable provider in an isolated
`HOME` (the shots in `2026-09-20-signals-gui` need none, because nothing there starts an agent), and
the only model this machine can reach without a key is the local one at four to five minutes a fix.
The three GUI surfaces have no branch the offscreen tests do not take, so what is unproved is the
wiring between a live worker event and those widgets — `BoardView::handleEvent`, which the
`boardsignals` test drives directly with the same event the worker emits.
