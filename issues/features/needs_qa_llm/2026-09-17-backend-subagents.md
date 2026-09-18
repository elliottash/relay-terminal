---
id: 2JY7
type: work
status: needs-qa-llm
labels: [feature]
component: [agent, worker]
milestone: desktop-alpha
workstream: agent (B, backend subagents)
assignee: implemented by Claude Opus 5 (Claude Code, backend B worktree), 2026-09-17
rank: fo
created: '2026-09-17'
acceptance: '`tests/test_agents_defs.py`, `tests/test_subagents.py`, live run in `docs/qa_evidence/2026-09-17-backend-subagents/`'
source: '`issues/features/2026-09-17-agent-sessions-planning-subagents.md` (owner decisions 4 and 5), `docs/AGENT-SESSIONS-PROTOCOL.md` sections 7 and 8'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Backend: subagents and agent definitions from all tools

## Behavior

Definitions (`backend/relay_core/agents_defs.py`):

- Loaded in this precedence order: `.relay/agents`, `~/.config/relay/agents`, `.claude/agents`, `~/.claude/agents`,
  `.opencode/agent`, `.opencode/agents`, `~/.config/opencode/agent`, `~/.config/opencode/agents`, `.codex/agents`
  (TOML), `~/.codex/agents`, `.gemini/agents`, `~/.gemini/agents`, `.cursor/agents`, `~/.cursor/agents`.
  Then the built-ins `explore` (read-only tools, effort low) and `general` (all subagent tools).
  `configure.agents.dirs` replaces the list. Subdirectories are scanned up to 4 levels deep.
- First name wins. Later duplicates, including built-ins that a user file replaces, go in `duplicates`.
  Invalid files go in `skipped` with a reason.
- Common model: name, description, prompt, tools, model, effort, max_steps, background, read_only.
  Sources: Claude Code `tools`/`disallowedTools`/`maxTurns`/`effort`/`background`; opencode path-derived names,
  `tools {x: bool}`, `permission` (`ask` and per-pattern rules count as not granted), `steps`/`maxSteps`,
  `variant` as effort, `mode: primary` and `disable` skipped; Codex `developer_instructions`,
  `model_reasoning_effort` (minimal→low, xhigh→max), `sandbox_mode = "read-only"`; Gemini `tools`, `max_turns`,
  `kind: remote` skipped; Cursor `readonly`, `is_background`.
- Tool mapping: Read→read_file; Bash, Grep, run_shell_command, grep_search→run_command (Grep grants the full
  shell and adds a warning); Glob, LS, list→list_directory; Write, Edit, MultiEdit, NotebookEdit, patch,
  replace→write_file; Skill→load_skill and read_skill_file; `*` means all. Other names are ignored with a warning,
  and patterns like `Bash(git:*)` are not granted.
- `model`: `inherit`; a Relay preset id; a preset model id (also `provider/model`); or an alias
  (haiku/sonnet/opus/fast, all `inherit` by default, overridable with `configure.agents.aliases`).
  A different preset uses its stored key. With no stored key, or an unknown model, the subagent uses the main model
  and `subagent_started.warnings` says so.
- `agents_list` → `agents {items: [{name, description, source, format, model, effort, tools, max_steps, background,
  read_only, warnings}], duplicates, skipped}`. `configured.agents` is the count.

Runtime (`backend/relay_core/subagents.py`; hooks in `agent.py`, `queue.py`, `worker.py`):

- Main-agent tools `agent {description, prompt, subagent_type, background?, model?, effort?}`,
  `agent_message {id, text}`, `agent_wait {id?, timeout_seconds?}`. Subagents never get these tools or
  `set_keybinding`; their executor refuses tools outside their definition.
- Each subagent is a thread with its own `Agent`, provider instance and cancel event, and an executor rooted at
  the same workspace. At most 4 run at once; extras wait (`subagent_progress` status `waiting`,
  last_activity "waiting for a free slot"). At most 16 can be live.
- All `agent` calls in one model response start before any is awaited, so foreground calls run in parallel.
  A foreground call blocks until its subagent finishes. A background call returns `{id, status: running}` at once.
- Results are labelled `[Result from subagent aN (type), outcome X: untrusted model output ...]`.
- Events: `subagent_started {id, type, description, background, model, effort, warnings?, resumed?}`,
  `subagent_progress {id, status, tools, tokens, tokens_estimated, elapsed_ms, last_activity}` (on tool calls and
  at most once a second otherwise), `subagent_finished {id, type, outcome, summary, handoff, wakeups,
  max_auto_turns, tools, tokens, elapsed_ms}`, and `subagent_handoff {id, handoff, wakeups, max_auto_turns}`
  when a deferred result is later queued or left pending.
- `agent_subscribe {id, on}` sends a `subagent_transcript {id, status, messages}` snapshot. While on, it wraps that
  subagent's delta, tool_started, tool_output, tool_result and status events in `subagent_event {id, payload}`.
- `agent_message {id, text}` from the user, or the tool from the main agent: a running subagent gets it before its
  next model call. A message that arrives after the last step starts another turn before the subagent is marked
  finished. A finished subagent resumes with the message in the background (`subagent_started` with
  `resumed: true`). Worker reply: `agent_message_delivered {id, delivered: next_step|resumed, status}`.
- `agent_stop {id|"all"}` → `agent_stopped {ids}`. Cancelling the main turn stops its foreground subagents.
  Background subagents keep running until `agent_stop`, `reset`, a new `configure`, or worker shutdown.
  Reset and configure also discard pending background results.
- `agents_status` → `agents_status {items}` lists all subagents.
- Background handoff: when a background subagent finishes (and no `agent_wait` is waiting for it), its result is
  held for the main agent and added as a labelled Relay-context user message before the next model call.
  If the main agent is idle, the worker queues a main turn with `when: queue` and `origin: "relay"`:
  "Background agent aN (type) finished: done." followed by a Relay-context block with the labelled result.
  If the main turn is running, the result is delivered before its next model call, or a turn is queued after it
  ends when no further model call happened. A main turn the user cancelled does not wake the agent; the result
  waits for the next turn.
- Automatic-turn cap (owner update): `configure.agents.max_auto_turns` and `set_agent_options {max_auto_turns}`
  (reply `agent_options {max_auto_turns, wakeups}`) take an int, default 50, 0 = unlimited. Every `ask` resets the
  counter. Past the cap, `subagent_finished.handoff` is `pending` and the result is delivered in the user's next turn.
- `queued` events and `queue_changed` items now carry `origin` (`user` or `relay`).

## Protocol deviations and additions

- `subagent_event {id, payload}`, not `{id, event}`: the protocol's `event` key collides with the envelope's
  `event` key.
- Values: `subagent_finished.outcome` uses `done|failed|stopped`, the same words as the progress status.
- `agent.background` is optional; it defaults to the definition's `background`, else false.
- `agent_wait` accepts `timeout_seconds` (1–1800, default 600) and returns `{agents: [...], timed_out}`.
- Added: `subagent_handoff`, `subagent_transcript`, `agent_message_delivered`, `agent_stopped`, `agents_status`,
  `set_agent_options` / `agent_options`, `configure.agents.aliases`, `configure.agents.max_auto_turns`, `origin`
  on queue events, and on `agents` the fields `duplicates`, `skipped`, `format`, `effort`, `max_steps`,
  `background`, `read_only`, `warnings`.
- The handoff turn text has a status word after the colon, and the labelled result follows on the next lines.
- The effort→provider table lives in `subagents.effort_extra` until backend A's presets table lands. Merge them then.

## Implementer check (not a QA verdict)

- `./scripts/test.sh`: 159 tests pass (12 new definition tests, 20 new runtime tests).
- Live run with Kimi K3 using the stored key: the main agent started `explore` in the background and ended its turn.
  The subagent listed and read the fixture files (2 tool calls, about 24 s) and finished with `handoff: wake`.
  Relay queued an `origin: relay` main turn, which ran and finished. Details are in
  `docs/qa_evidence/2026-09-17-backend-subagents/NOTES.md`.

## Gaps

- No GUI yet (workstream E): the subagent list, transcript pane, stop keys, and the options field for max_auto_turns.
- Token counts are estimates when the provider stream has no `usage` (Kimi); reasoning tokens are not counted.
- No per-path write lease across agents. Concurrent writes rely on the existing hash check in `write_file`,
  which fails with an error instead of overwriting.
- No worktree isolation, no `maxTurns` above 50, no skills preloading from definitions, no hooks or MCP fields.
- Claude Code's walk up parent directories for `.claude/agents` is not done; only the workspace root is read.
- Definitions load at `configure`; changed files need a reconfigure.

## QA checklist

1. Put agent files in `.claude/agents`, `.opencode/agent`, `.codex/agents` (TOML), `.gemini/agents`, and
   `.cursor/agents`; `agents_list` shows each with its format, mapped tools, and warnings, and a duplicate name
   is reported.
2. Ask the agent to run two `explore` tasks in parallel; both `subagent_started` events arrive before either
   finishes, and the main answer uses both results.
3. Start a background subagent and let the main turn end; on completion a `relay`-origin turn runs by itself.
4. While the main agent is busy with a long tool loop, a background result appears in its next step and no
   extra turn is queued.
5. `agent_subscribe` on a running subagent streams its tool events; turning it off stops them.
6. `agent_message` to a running subagent changes what it does next; to a finished one, it resumes.
7. `agent_stop` for one id and for `all`; cancelling the main turn stops foreground subagents but leaves
   background ones running.
8. Set `max_auto_turns` to 1, finish two background agents while idle: one automatic turn runs, the second
   result is `pending` and is used in the user's next prompt.
9. A subagent cannot call `agent` and an `explore` subagent has no `write_file`.
10. Six background agents: four run, two wait, and all finish.
