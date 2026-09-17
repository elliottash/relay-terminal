---
id: SQAM
type: work
status: needs-qa-llm
component: [gui, worker]
milestone: desktop-alpha
workstream: agent
assignee: implemented by Claude Opus 5 (1M context), Claude Code session, 2026-09-17
rank: zzz
created: '2026-09-17'
labels: [bug]
acceptance: a stalled provider connection ends the turn with a clear message within a bounded time, the pane shows how long the model has been thinking, and a rotating log file exists with enough detail to diagnose it afterwards
source: 'owner in chat, 2026-09-17: "also are you keeping logs? check how i have been using it. there was a connection failure, and then another agent seems to be stuck in thinking"'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-17-provider-stalls-and-no-logs/'], related: [], github: null}
---
# Provider stalls have no visible progress, no retry and no log

## What happened (observed 2026-09-17 15:37 on the owner's machine)

- A GLM-5.3 pane running "implement the plan for verbal transcription" (plan mode) made ~30 tool calls, then the
  turn ended with `Provider connection failed (TimeoutError)`. The prompt and the "not finished" note were kept
  in the conversation, which is the intended drop-path behaviour.
- A second pane's worker (pid 689705) still held an ESTABLISHED TCP connection to the Z.AI endpoint opened at
  15:25, with an empty receive queue 12 minutes later, while the pane showed only "thinking". The socket
  outlived the 30 s read timeout, so either the timeout does not apply to the streaming read or the socket was
  leaked after the error.
- **Relay keeps no log at all.** `~/.local/share/relay/` holds only `sessions/` and `state/`; the process
  writes to stderr, which is lost when it is started from a launcher. There was nothing to look at afterwards.

## Scope

1. **Bounded stalls**: make the read timeout apply to every streamed chunk; when nothing arrives for N seconds
   (configurable, default ~60 s for reasoning models), end the turn with "the model sent nothing for N s", and
   close the socket for certain. Retry a timed-out turn once automatically before failing, keeping the request
   open in the ledger.
2. **Visible progress**: while a turn is in flight with no output, the pane shows elapsed time ("thinking · 48 s")
   and a reminder that Esc stops it; the thinking overlay already exists for models that stream reasoning.
3. **Logs**: a rotating log (e.g. `~/.local/share/relay/logs/relay.log` and `worker.log`, ~5 MB × 3) with
   timestamps, pane and session ids, provider host and model, turn start/end, tool names, event types, errors
   and timings. **No prompts, no tool output, no file contents, no keys** by default; a "verbose" level that
   includes prompt text is opt-in and documented as such. A palette action opens the log folder.
4. **Socket hygiene**: verify no connection outlives its turn (the observed one did); add a test or a check.

## Implemented (2026-09-17)

### Why the old timeout could not work

`urlopen(timeout=N)` *does* apply to each read of the response, but any byte resets it. Measured on
loopback: a stream sending `: keepalive` every 0.2 s read for 8 s against a 2 s timeout without ever
raising, while a byte-silent stream raised at 2 s. An SSE stream of comments or empty deltas is all
bytes and no answer, which is how a worker held an ESTABLISHED connection with an empty receive
queue for 12+ minutes. Widening the timeout (the `RELAY_PROVIDER_TIMEOUT` stopgap) only widens the
window; the deadline has to run from the last *usable* chunk.

### 1. Bounded stalls

- New `stall_timeout_s` (agent option, 1–1800 s, **default 60**): how long the model may send
  nothing usable — no answer text, reasoning, tool-call fragment, `usage` or `[DONE]`. Keepalive
  comments and empty deltas do **not** reset it.
- Enforced by a watchdog thread that closes the response; the socket timeout stays as a backstop.
  The same deadline is also the budget for the response headers (`max(30 s, stall_timeout_s)`),
  because a provider may withhold its `200` until the first token — that was the owner's
  `Provider connection failed (TimeoutError)`, which now reads as a stall and is retried.
- `RELAY_PROVIDER_TIMEOUT` (5–900 s) folds the stopgap in as an override of the same one deadline.
  GUI: Actions › Diagnostics › "Stop a silent model after…" (`agent/stall_timeout_s`), applied to a
  configured agent at once.
- On expiry the response is closed (`shutdown()` + `close()`) and the turn ends with
  `Provider stalled: the model sent nothing for 60 s.` The request stays **open** in the ledger and
  the model gets the usual "not finished" note.

### 2. Retry once — the rule chosen, and why

Retried **once**, and only when the stalled response produced **no answer text and no tool-call
fragment**. A stall can only happen while waiting for the model, at a step boundary where every
earlier tool call already has its result in the conversation: nothing is in flight, so the retry
repeats no side effect and re-sends a byte-identical conversation (asserted in
`test_retried_once_and_the_second_try_sends_the_same_conversation`). Reasoning-only output still
allows the retry, because the thinking overlay is closed and reopened; a **started answer does
not**, because that text is already on the user's screen and repeating it would show two answers.
New event `provider_retry {turn_id, reason, attempt, max_attempts, seconds, step, text}`; the pane
prints it as a `⚠` note and the ledger entry stays `in_progress`.

### 3. Visible progress

While a turn is in flight the existing status line counts up — `thinking · 23 s · step 1/50 · Esc
stops` (the step note rides along instead of replacing it, and the key comes from the live Keymap).
The existing thinking overlay's header gains the same elapsed time. No new widget.

### 4. Rotating logs

`~/.local/share/relay/logs/relay.log` (GUI, `src/Logging.cpp`) and `worker.log` (all workers,
`backend/relay_core/logs.py`), 5 MiB × 3 backups, files `0600` in a `0700` directory. One line
format, `<ISO-8601 UTC> <LEVEL> <logger> pane=<id> <event> key=value …`, with the same pane id on
both sides. Workers share `worker.log`: each record is written under an advisory lock on a hidden
`.worker.log.lock`, and a handler whose file another worker rotated reopens it.

Logged: timestamps, pane and session ids, provider host and model, turn start/end and outcome, tool
names with durations and exit codes, event types, errors with type and message, stall and retry
decisions, worker start/exit. **Never logged at any level:** prompts, answers, reasoning, tool
arguments, tool output, file contents, terminal output, API keys, password-mode input.
`logs.scrub()` masks credential-shaped text in every record as a second line of defence.

Levels `off | error | info | debug | verbose` (Actions › Diagnostics › Log detail, setting
`logging/level`, passed to workers as `RELAY_LOG_LEVEL`). **`verbose` is the one level that writes
prompt text**; it is off by default, labelled "Verbose (includes prompt text)" in the palette, and
documented as such in README and `docs/ARCHITECTURE.md`. Actions › Diagnostics › Open log folder.

### 5. Socket hygiene — what the check found

`ChatProvider.response_open()` reports whether a response is still held; the agent calls it at every
turn end (done, cancelled, error, limit) and before each retry, closes anything still open and logs
`provider_response_left_open`. `cancel()` is synchronous now (`shutdown()` cannot block), so the
socket is gone when it returns instead of "best effort" on a detached thread; `complete()` also
hard-closes in its `finally`, and an `HTTPError`'s response is closed rather than left to the GC.

The check found **no leak left** once the deadline was enforced: across a real worker's stall,
retry and failure, `/proc/<worker pid>/fd` held **no sockets** after the turn — measured the same
way the original leak was found. The two provider-side connections (one per attempt) were both
closed by the worker. The original leak was not a missing `close()` but a read that never returned,
so nothing ever reached the close.

### Files

`backend/relay_core/{provider,agent,logs,session_protocol}.py`, `backend/worker.py`,
`src/{Logging.cpp,Logging.h,main.cpp}`, `CMakeLists.txt`,
`tests/{test_provider,test_agent,test_logs}.py`, `tests/logging_test.cpp`,
`docs/{AGENT-SESSIONS-PROTOCOL,ARCHITECTURE}.md` (protocol section 15, architecture 13a), `README.md`.

## Implementer check (not a QA verdict)

`./scripts/test.sh` 439 tests OK (was 420); `ctest --test-dir build` 13/13 (was 12, + `logging`);
`cmake --build build` with no new warnings. Live under Xvfb with an isolated
`XDG_CONFIG_HOME`/`XDG_DATA_HOME` against a loopback endpoint that sends headers then only
keepalives: the status line counted up, the turn was retried once at 60 s and failed at 120.067 s,
the task stayed unfinished, and both log files recorded it without the prompt text.
Evidence: `docs/qa_evidence/2026-09-17-provider-stalls-and-no-logs/`.

## Deviations

- The card asked for `worker.log` "per worker, or one file with pane ids"; one shared file with
  pane ids was chosen, with an advisory lock around rotation, because per-pane files would
  accumulate one file per pane per launch.
- The default deadline is 60 s as the card asked, not the stopgap's 120 s, because it now measures
  *usable* output rather than time since connect and a stall is retried before it fails.
- A connect-stage stall (no response headers at all) shares the mechanism and reports
  "the provider did not answer within N s"; it is retryable for the same reason.

## Gaps

- Changing Log detail applies to the GUI at once but a worker reads `RELAY_LOG_LEVEL` at startup,
  so an already-running pane keeps its level until its worker restarts (said so in the menu).
- Rotation of `worker.log` is safe between workers through an advisory lock; a worker killed mid
  `-9` while holding the lock releases it with the fd, but a record being written can be truncated.
- Subagent turns log through the same agent code path but are not separately tagged as subagents.

## QA checklist

1. With a provider that stalls (or a loopback endpoint that sends SSE headers then only
   `: keepalive`), submit a prompt: the status line shows `thinking · N s · … · Esc stops` counting
   up, the turn is retried once with a `⚠ Provider stalled…` note, and fails after
   2 × the deadline with the request left unfinished in `/tasks`. Esc still stops it at any point.
2. A normal turn against a real provider is unaffected: reasoning, tool calls and the answer stream
   as before, and a long quiet reasoning burst does **not** trigger the deadline.
3. Actions › Diagnostics › "Stop a silent model after…" changes the limit and takes effect on the
   next turn without restarting the pane; an out-of-range value is refused.
4. `~/.local/share/relay/logs/` exists with `relay.log` and `worker.log`, both `-rw-------`, the
   directory `drwx------`. Grep both for a prompt you typed, for tool output and for your API key:
   none appear. `pane=` matches between the two files for the same pane.
5. Set Log detail to "Verbose (includes prompt text)", restart the pane's worker (new chat is
   enough to see GUI lines; the worker level follows a restart), run a turn: prompt text now appears
   in `worker.log`. Set it back to Normal and confirm new turns no longer record it.
6. Set Log detail to Off: no new lines are written.
7. Force rotation (e.g. `yes | head -c 6M >> relay.log`, then use Relay): `relay.log.1` appears and
   at most `.1`–`.3` are kept.
8. Actions › Diagnostics › Open log folder opens the directory in the file manager.
9. While a turn runs, run `ss -tnp | grep <worker pid>` (or check `/proc/<pid>/fd`): one connection
   to the provider. Let the turn end normally, cancel one with Esc, and let one stall — after each,
   the worker holds no socket to the provider.
10. Two panes running turns at once both write to `worker.log` with their own `pane=` ids and
    neither file shows interleaved half-lines.
