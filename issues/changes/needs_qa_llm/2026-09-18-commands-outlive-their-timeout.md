---
id: P1CE
# Renumbered 2026-09-18: U is not Crockford base32, so no board tool could address YXMU.
aliases: [YXMU]
type: work
status: needs-qa-llm
labels: [change, feature]
component: [backend, gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (Claude Code session), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: 'A run_command past its wait comes back as a job the agent can read (command_output) or stop (stop_command); background: true starts servers; timeouts are clamped to 1–1800 s instead of refused; jobs end with their conversation; tests/test_jobs.py passes; a QA session confirms a long build and a background server live'
source: 'owner in chat, 2026-09-18, after "✗ Timeout must be an integer from 1 to 120 seconds." (glm-5.3 asked 180 s for a cmake build): "why have a maximum at all" → "i want these recommendations as well as the bigger option now"'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-commands-outlive-their-timeout/'], related: [], github: null}
---
# Commands outlive their timeout

## What was wrong

`run_command` killed a command at `timeout_seconds` and refused anything over 120 s. Models asked
for the ceiling constantly (39 of the 49 explicit timeouts in the owner's sessions were 120), and
a request for 180 s was an error line and a wasted turn.

## The change

- `backend/relay_core/jobs.py`: every command is a job with its own process group, output kept
  (last 1 MiB) and read incrementally. At most 8 run at once.
- `run_command` waits up to `timeout_seconds` (default 30, clamped to 1–1800, numeric text
  accepted) and hands a still-running command back as `{still_running, job_id, output, note}`.
  `background: true` returns after 2 s for servers and watchers.
- New tools `command_output {job_id, wait_seconds}` and `stop_command {job_id}`; subagents with
  run_command get them too.
- Stop ends the command a call is waiting on; jobs handed back earlier keep running. A new
  conversation, the end of a subagent's run and the worker's exit stop every job.
- The pane prints `▸ still running as job-N` and `■ stopped job-N`. System prompt and tool
  descriptions explain the jobs; protocol in `docs/AGENT-SESSIONS-PROTOCOL.md`.

## The jobs list (same day, owner: "add this suggestion")

A job the model was handed back is listed under the prompt box, beneath the running-agents list
(`src/JobsPanel.{h,cpp}`: `JobsModel` + `JobsPanel`, in the `relay-subagents` library). It takes
layout space like that list — the terminal gives up a row or two, nothing is covered — and hides
when empty. Down from the prompt (or past the last agent) enters it; Enter opens the job's output
in a file pane (`command-job-N.log`, the newest 256 KiB, without moving the model's read position);
x or Delete stops a running job or dismisses a finished one; Esc or Up leaves. Clicking the ×
shows a "Next time" hint. Finished rows stay until dismissed or the next prompt; a job that ends
by itself says so in the status bar. Commands that finish inside their own call never appear.

Worker: `jobs` events on every change; `jobs_list`, `job_output_get` → `job_output`, `job_stop`
(`observe_protocol.py`). `jobs` is forwarded to phones like `tool_result`; `job_output` is
withheld. Also fixed on the way: the job table kept every finished job (every command is a job)
with up to 1 MiB each; it now remembers the 16 most recent.

Tests: `tests/test_jobs.py` (23), `tests/jobs_test.cpp` (model, keys, the ×). Live, glm-5.3: the
agent started `python3 -m http.server 8766` and left it; the list showed `job-1 … running`
(`jobs-list-running.png`); Down, Enter opened its output; Down, x stopped it
(`jobs-list-stopped-with-x.png`) and nothing listened on 8766 afterwards.

A subagent's commands are not listed: they end with its run, which the running-agents list shows.

## QA checklist

- [ ] Ask for a build that takes over 30 s: it comes back as a job and the agent waits for it.
- [ ] Ask for a dev server, a curl against it, then stop: nothing left listening.
- [ ] Start a background job, then /new: the process is gone.
- [ ] Stop during a long foreground command ends that command.
- [ ] A background job appears under the prompt; Down, Enter shows its output; x stops it.
- [ ] Click the × on a running job: it stops, and the "Next time" hint appears once.
