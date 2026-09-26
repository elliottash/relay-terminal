---
id: 12JX
type: work
status: needs-verification
labels: [feature, agent, sessions]
assignee: agent
implemented_by: glm/glm-5.3
session: b4bd3aae-0646-4c2f-91d4-6d149a1c1b0e
rank: zzzzzzzzzzzzzzzzzzzzy
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Subagents survive a Relay close or crash and restore in the same state

## Issue
feature card: how are subagents treated when relay closes or crashes while they are running. ideally they are restored in the same state
Today nothing restores a subagent after Relay closes or crashes, whatever the close mode:

- **Clean close** — each pane's worker (a Python child process, one per pane) gets `cancel` then `shutdown` (`Pane::~Pane`, `src/PaneRuntime.cpp`; `backend/worker.py` `"shutdown"` handler), which also releases the pane's board claims. `subagents.shutdown()` → `stop_all(reset=True)` finishes every running thread as `status: stopped` and saves its partial transcript durably to `<session>.threads/<thread-id>.json` (`backend/relay_core/subagents.py` `_finish_locked`/`_save_thread`, durable threads #Y63Z). The in-flight parent turn is cancelled; the session file keeps the last completed turn.
- **GUI crash** — same outcome: the worker sees stdin EOF, breaks its read loop and runs the same shutdown, so threads land as `stopped` too. Window layout is debounced at ~1 s (`src/WindowManagerImpl.h`), the session file saved at turn boundaries.
- **Worker killed outright** (OOM kill of its isolation unit, SIGKILL, power loss) — the thread file keeps whatever was last written: `status: running` from the spawn-time save (`_bind_thread`), because the next save only happens at run end (`_finish_locked`). The mid-run transcript is lost and the file reads `running` forever.

After restart: panes and their conversations restore (`resume` → `state_loaded`, `Pane::resumeRestoredSession`, `src/Pane.h`), but a tab holding only a subagent transcript is left out of the saved layout (`restorableTabs`, `src/RelayWindow.h`), and the worker's subagent roster starts empty. Saved threads are listed (session ⓘ info, Sessions search) and openable as transcripts, but cannot be continued: `agent_message` on a stale id answers "Unknown subagent", and `spawn` has no continue-from-thread path. The thread record (version, messages, model, effort, todo link) already carries what a rebuild needs.

## Discussion points
- What should "restored in the same state" mean for a thread that was mid-run when Relay went away?
  (a) auto-resume the run where it stopped, (b) come back idle with its history so the parent can `agent_message` it onward, (c) smallest: mark orphaned `running` files interrupted and offer Continue in the transcript view.
- Same-state across a *hard kill* needs mid-run checkpoints: the thread file is written at run start and run end only, so restore can never be better than the last durable save without per-message saves.
- A restored parent turn that was blocked on a foreground subagent is a separate decision: replay the wait, or surface "this turn was interrupted" and let the parent re-issue.

## Done means
Proposed, pending the decision above: after a clean close or a crash, restarting Relay restores the panes that ran subagents, and every thread that had not finished is back in the roster in a defined state (resumed, or idle-with-history) that the parent can address with `agent_message`; a thread interrupted by a hard kill never stays showing `running`. Tests cover clean close mid-foreground-run, GUI crash (worker EOF), worker kill, and restart-then-continue.

## Decisions
2026-09-25, the owner: "yes, your rec is good" — option (b): restore interrupted subagents idle with their history, addressable by the parent via `agent_message`; no auto-resume of model spend. Hard-killed threads that never got a clean stop get (c)'s interrupted marker instead of staying `running`.

## Plan
Restore happens in the worker, at the moment a conversation is resumed (#12JX, decision (b)+(c) fallback):

1. `SubagentManager.restore_threads(main)` in `backend/relay_core/subagents.py`: list `main.store.threads(main.session_id)`; for every thread whose saved status is not terminal (`done`/`failed`/`limit`/`blocked`), rebuild it into the roster on its saved `agent_id` — same factory path as `spawn` (definition from the catalog, model/effort from the file), messages seeded behind the rebuilt system prompt, `usage_totals`/`models_used` from the file — and leave it idle. Threads saved `running`/`waiting`/`paused` (a hard kill: no clean stop was ever written) are re-saved as `interrupted` and restored with that status; `stopped` threads (a clean close) keep `stopped`. Already-present agent ids are skipped, and `_next` moves past every restored id.
2. `agent_message`/`agent_wait` need no change: a restored sub is non-live, so `send_message` already resumes it (waiting → run with history) and `wait` returns immediately.
3. Hook: `session_protocol._resume` calls `restore_threads(agent)` after a successful `agent.resume()`, wrapped so a restore failure never breaks the resume; the `state_loaded` reply carries `threads_restored`.
4. Roster event: one `subagent_started` with `restored: true` per restored thread so the panel and transcript tabs list it.
5. Tests in `tests/test_session_threads.py` (store + manager harness lives there): stopped-after-close restores idle and continues via `agent_message` with its history; a `running` file restores as `interrupted` on disk and in the roster and `wait` returns; terminal threads are not restored.

Out of scope, noted: mid-run checkpoints (per-message saves) so a hard kill loses less than a whole run; the interrupted parent turn itself still restores as today (last completed turn).

## Tasks
- [x] restore_threads(main) in SubagentManager: rebuild non-terminal threads idle on their saved agent_id, interrupted marker for live-status files <!-- t:hc -->
- [x] Hook restore into session_protocol._resume after agent.resume(); threads_restored in the state_loaded reply <!-- t:k3 -->
- [x] Tests: stopped restores idle and continues via agent_message; running restores interrupted (file + roster); terminal not restored <!-- t:jk -->
- [x] Run the subagent + session-thread test files; land via scripts/land.py with #12JX <!-- t:p4 -->


## Execution Summary
Landed as `aefbea87` on main (verify-slot build of the exact tree passed).

- `SubagentManager.restore_threads(main)` + `_restore_thread` in `backend/relay_core/subagents.py` (+95): rebuilds every not-yet-finished thread of the resumed session into the roster on its saved `agent_id` — definition from the catalog, model/effort from the file, messages replayed behind the rebuilt system prompt, `usage_totals`/`models_used` restored, `done` set (idle). Files saved at a live status (`running`/`waiting`/`paused` — the worker died before writing a stop) are re-saved and restored as `interrupted`; `stopped` keeps `stopped`. Idempotent (agent ids already in the roster are skipped) and `_next` moves past restored ids. Emits `subagent_started` with `restored: true` per thread.
- `session_protocol._resume` calls it after `agent.resume()` (try/except: restore can never break the resume) and adds `threads_restored` to the `state_loaded` reply when non-zero.
- `src/SubagentsPanel.cpp`: `interrupted` gets a `⊘` glyph.
- No change needed to `agent_message`/`agent_wait`: a restored sub is non-live, so the existing resume path in `send_message` continues it with history, and `wait` returns immediately.

Deliberately out of scope (recorded in Plan): mid-run checkpoints so a hard kill loses less than a whole run, and replaying a parent turn that was blocked on a foreground subagent — both their own cards if wanted.

## Tests
`tests/test_session_threads.py`, new `RestoreThreadsTests` (4 tests, all passing, run with `PYTHONPATH=backend python3 -m unittest tests.test_session_threads`):

- `test_a_stopped_thread_comes_back_idle_and_continues_after_a_close` — gated background subagent, manager shutdown (the clean close) saves it `stopped`; a second manager over the same store restores it (`restored: true` event, id `a1`, status `stopped`), and `send_message("a1", "go on")` finishes it: file `done`, `runs == 2`, the reply reports `last=go on` over the original task — history intact.
- `test_a_thread_the_kill_left_running_restores_interrupted` — a `running` thread file (no stop ever written) restores as `interrupted` in the roster and on disk, and `wait` returns immediately, not timing out.
- `test_finished_threads_stay_records` — `done` threads are not restored (`0`, empty roster).
- `test_resume_brings_the_threads_back_with_the_conversation` — `SessionCommands.handle("resume", ...)` sets `threads_restored: 1` on `state_loaded` and fills the roster.

Suites: `tests.test_session_threads` (19) and `tests.test_subagents` (50) green. `tests.test_session_protocol` + `tests.test_sessions` (92) show one failure, `test_compact_resume_recap_and_plan_execute`, which reproduces on a pristine `git archive HEAD` export — pre-existing on main, filed as #BCJF, not from this change. C++ side: the land.py verify slot built the exact tree (`--target relay`) before the swap.

Follow-up `c2792ff7` (found by running the Try it check twice on one sandbox): a thread file already `interrupted` — restored once, Relay closed again — degraded to `stopped` on the next restore. `interrupted` now persists across further restarts; only `running`/`waiting`/`paused` on disk mark a kill and get re-saved. `test_a_thread_the_kill_left_running_restores_interrupted` now covers the second restart. `tests.test_session_threads` (19) green after the change; the staged `check.sh` passes twice consecutively on one sandbox.

## Try it
Staged for you in `docs/qa_evidence/2026-09-25-subagent-restore/` (landed with `c2792ff7`):

```
bash docs/qa_evidence/2026-09-25-subagent-restore/stage.sh     # a killed-mid-run sandbox
bash docs/qa_evidence/2026-09-25-subagent-restore/check.sh     # proves the restore, no GUI
XDG_DATA_HOME="$PWD/docs/qa_evidence/2026-09-25-subagent-restore/sandbox/home" \
  RELAY_KEYRING=off RELAY_MEMORY_IMPORT=off ./build/relay
```

The staged pane resumes its conversation ("Pelican survey — killed mid-run") and brings subagent `a1 — Count the pelicans` back with it. What to look at is in the sealed `expected.md` beside the scripts; close the app and `bash …/unstage.sh` when done. `check.sh` passes twice in a row on one sandbox (second restart keeps `interrupted`) — that rerun is what found the status-degradation fix in `c2792ff7`.
