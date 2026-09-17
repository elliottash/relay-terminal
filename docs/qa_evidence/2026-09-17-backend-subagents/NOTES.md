# Backend subagents: implementer evidence (2026-09-17)

Not a QA verdict. Branch: the backend B worktree, merged by the main session.

## Unit tests

`./scripts/test.sh`: 159 tests, all pass. New:

- `tests/test_agents_defs.py` (12): all 14 locations load (Relay, Claude Code, opencode `agent` and `agents`,
  Codex TOML, Gemini CLI, Cursor; workspace and home), first source wins with duplicates reported (including a
  user `explore` replacing the built-in), explicit `agents.dirs`, skipped files with reasons, Claude Code
  `tools` as a string and as a list plus `disallowedTools`, tool-name mapping and warnings (Grep maps to the full
  shell, `Bash(git:*)` patterns are not granted, MCP and WebFetch ignored), opencode path-derived names and
  `tools`/`permission`/`mode: primary`/`disable`, Codex `sandbox_mode = "read-only"` and `xhigh` effort, Gemini
  `kind: remote` skipped, Cursor `readonly`/`is_background`, the YAML subset, and a worker `agents_list` run.
- `tests/test_subagents.py` (20): three foreground `agent` calls in one response run at the same time (fake
  provider saw 3 active), concurrency cap (6 background, 4 active, 2 waiting), no nesting and tool restriction,
  progress with tool and token counts, main-turn cancel stops foreground but not background, invalid calls,
  stop one and all, a message reaching a running subagent at its next step, a message resuming a finished
  subagent, subscribe stream (`subagent_transcript`, wrapped events, stops when unsubscribed), background
  completion while idle (queued turn with `origin: "relay"`), while busy (injected before the next model call,
  no extra turn), finishing after the main agent's last model call (wake after the turn), the automatic-turn
  cap leaving the result pending until the user's next turn, 0 = unlimited, a cancelled main turn not waking,
  `agent_wait`, reset dropping pending results, worker `set_agent_options`/`configure.agents.max_auto_turns`,
  model and effort resolution.

Flakiness check: `tests.test_subagents` run 30+ times in a loop after fixing one test-ordering race.

## Live check (Kimi K3, stored key)

Script: `live_check.py` in this folder. It starts `backend/worker.py`, configures preset `kimi` with
`use_stored_key`, and creates a scratch workspace with `fixture/alpha.txt`, `beta.txt`, and `notes.md`. It sends:

> Use the explore subagent in the background (background: true) to list and summarize the files in the
> fixture directory. Do not read the files yourself. After starting it, just tell me it is running and end your turn.

It subscribes to the first subagent. Filtered event log (deltas omitted): `live-kimi-events.log`.

Observed (second run, final code):

1. 09:47:49 `configured` with `agents: 2` (built-ins only; no agent files on this machine).
2. 09:48:00 `subagent_started {id: a1, type: explore, background: true, model: kimi-k3, effort: low}`; the main
   agent's `agent` tool result was `status: running` at once.
3. 09:48:07 the main turn finished (`agent_finished` done) while a1 was still running.
4. a1 made 2 `run_command` calls (ls/head of the fixture files), streamed as `subagent_event` wrappers (15), with
   13 `subagent_progress` events.
5. 09:48:24 `subagent_finished {outcome: done, handoff: wake, wakeups: 1, max_auto_turns: 50, tools: 2,
   tokens: 224, elapsed_ms: 24403}`, summary listing all three files with correct contents.
6. Then `queued {when: queue, origin: relay}`, `agent_started`, and at 09:48:34 `agent_finished` done: the
   automatic main turn ran.

First run (before the event-order fix) showed the same flow, with `queued` emitted just before
`subagent_finished`; the order is now finished, then queued. In that run the subagent first tried an
absolute path with `list_directory`, got the workspace-path error, and recovered.

Notes from the live runs:

- Kimi's stream sent no `usage`, so token counts are estimates from visible content (`tokens_estimated: true`) and
  leave out reasoning tokens. Requesting `stream_options.include_usage` belongs in provider.py.
- `reasoning_effort: low` was accepted by Kimi K3 (no HTTP error).
