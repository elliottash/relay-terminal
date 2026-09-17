---
id: SQAM
type: work
status: ready
component: [gui, worker]
milestone: desktop-alpha
workstream: agent
rank: zzz
created: '2026-09-17'
labels: [bug]
acceptance: a stalled provider connection ends the turn with a clear message within a bounded time, the pane shows how long the model has been thinking, and a rotating log file exists with enough detail to diagnose it afterwards
source: 'owner in chat, 2026-09-17: "also are you keeping logs? check how i have been using it. there was a connection failure, and then another agent seems to be stuck in thinking"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
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
