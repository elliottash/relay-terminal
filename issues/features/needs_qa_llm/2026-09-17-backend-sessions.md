---
id: BXPG
type: work
status: needs-qa-llm
labels: [feature]
component: [worker, agent]
milestone: desktop-alpha
workstream: agent (A, backend sessions)
assignee: implemented by Claude Opus 5 (Claude Code, backend sessions worktree), 2026-09-17
rank: f3
created: '2026-09-17'
acceptance: '`tests/test_sessions.py`, `tests/test_session_protocol.py`, updated `tests/test_skills.py`; live worker run in `docs/qa_evidence/2026-09-17-backend-sessions/`'
source: '`issues/features/2026-09-17-agent-sessions-planning-subagents.md`, protocol `docs/AGENT-SESSIONS-PROTOCOL.md` sections 1-7 (without subagents), 9, 10; research defaults from `docs/INTAKE-CLARIFICATION-RESEARCH.md`'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Agent sessions backend: model/effort, context and compaction, checkpoints, sessions, recaps, plan mode, instructions, suggestions

## Behavior

Worker protocol (all additive; GUI not wired yet):

- **configure** accepts `preset`, `context_window`, `effort`, `compact_threshold` (0.5–0.98, default 0.80),
  `session_dir` (default `$XDG_DATA_HOME/relay/sessions/<sha256(workspace)[:16]>`), `plans_dir`
  (default `<workspace>/.relay/plans`), `instructions {files, project_auto (default true), max_bytes (default 32768)}`,
  `skills.exclude`. `configured` adds `context_window, compact_threshold, limit_tokens, effort, mode, instructions,
  instructions_max_bytes, instructions_bytes, instructions_truncated?, instructions_skipped?, session_id, plans_dir, session_dir`.
- **set_model** swaps the provider between turns keeping the conversation (refused while busy); earlier assistant
  messages are adapted (`reasoning` ↔ `reasoning_content`) so Kimi/GLM/OpenRouter accept each other's history.
  → `model_changed {model, preset, context_window, effort}` + `context`.
- **set_effort** → `effort_changed {effort, applied}`. Table in `backend/relay_core/presets.py`, verified against provider
  docs: Kimi `reasoning_effort` low/high/high/max; GLM `thinking:{type:enabled}` + `reasoning_effort` low/high/high/max;
  OpenRouter `reasoning.effort` low/medium/high/xhigh. Context windows: Kimi 1,048,576; GLM-5.3 1,000,000 (Z.AI);
  DeepSeek V4.1 Flash 1,048,576; unknown 128,000.
- **context**: emitted after every model response and on demand: `{used_tokens, window, percent (0–100), threshold,
  limit_tokens, estimated}`. Usage from the provider plus a scaled ~4 chars/token estimate for newer messages.
- **Compaction** at step boundaries when `used >= min(threshold × window, window − max_tokens − 24000)`: trim old tool
  outputs (>1 KiB → `{elided, bytes, head}`), then a no-tools summary of turns before the last 2 (cut only at user
  messages, so tool-call groups stay whole). `compact {focus?}` runs it manually in the background.
  → `compaction_started {reason}`, `compacted {reason, before_tokens, after_tokens, summary_chars, trimmed_tool_outputs}`.
- **Checkpoints**: per user turn; pre-images of `write_file` targets stored content-addressed in `<session>.blobs/`.
  `checkpoints` → items `{turn, prompt_preview, time, files, conversation}`. `rewind {turn, restore}` restores files whose
  hash still matches what the agent wrote (others → `conflicts`), truncates the conversation (via a stored pre-compaction
  snapshot if needed), and prepends a rewind note to the next user turn. → `rewound {turn, restore, restored_files,
  conflicts, note, prompt}`.
- **fork {turn?}** (includes that turn) → `fork_state {state}`; **load_state {state}** → `state_loaded {session_id, turns, model, title}`.
- **Sessions** auto-save after every turn (0600 files, 0700 dir). `sessions` → `{items: [{id, title, updated, turns, model}]}`;
  `resume {id}` → `state_loaded`, `mode_changed`, `context`, then `recap`. `reset` starts a new session.
- **Recaps**: `recap_request {reason: away|resume|manual}` → `recap {text ≤700, next_action ≤200|null, turns_covered, reason}`
  or `recap {skipped: too_few_turns|no_turns|failed}` (away needs ≥3 turns; other gating is GUI-side).
- **Plan mode**: `set_mode {mode}` → `mode_changed`. Plan mode removes `write_file`/`set_keybinding` (also refused at
  execution), keeps `run_command` for read-only investigation, adds `write_plan {title, content}` which writes
  `<plans_dir>/<YYYY-MM-DD-HHMM>-<slug>.md` (dirs created, never overwrites) → `plan_written {path, title}`.
  `plan_execute {path, fresh, when?}` re-reads the plan from disk, resets if fresh, switches to build and queues
  "Execute the plan in <path>:\n<content>".
- **Instructions**: `scan_instructions {workspace}` → `instructions_found {items: [{path, tool, scope, bytes, exists}]}`
  over the research table (Claude Code, Codex, Warp incl. `~/.warp/WARP.md`, opencode, Gemini, Cursor, Copilot, Windsurf,
  Devin, Cline, Zed, Junie, Kiro, Continue, aider). Project files from git root to workspace; `project_auto` loads the first
  per directory (WARP.md > AGENTS.override.md > AGENTS.md > CLAUDE.md …, plus CLAUDE.local.md and unconditional
  `.claude/rules`), resolves `@imports` (project imports must stay in the project; global ones under $HOME; secret paths
  refused), labels each block with its path, and caps the section. `synthesize_instructions {files, target?}` → no-tools
  merge written to `target` (default `~/.config/relay/relay.md`, old file kept as `.bak`) → `instructions_synthesized {path, bytes}`.
- **Attachments**: `ask.attachments: [{path}]` (absolute or workspace-relative; files ≤128 KiB each, 256 KiB total, ≤10;
  directories as listings; binary refused at submit time) prepended as labelled blocks.
- **Suggestions**: `suggest {kind: next_command, command, exit_status, cwd, output_tail?}` and `suggest {kind: next_prompt}`
  → `suggestion {kind, id, text, reason}` using a cheap provider (effort low, ≤4096 output tokens); empty `text` means none.
- **Skills (owner update)**: default search `~/.warp/skills`, `~/.claude/skills`, `<workspace>/.claude/skills`, then every
  `<name>/SKILL.md` folder under `~/.warp` (depth 6, no symlink walking); first name wins; Warp-app skills (warpctrl,
  oz-platform, create-tab-config, update-tab-config, modify-settings, add-mcp-server, factory-mcp) excluded by default and
  reported in `skills_skipped`.

## Implementer check (not a QA verdict)

- Rebased onto main after the subagents merge (dec2d2d); `./scripts/test.sh`: 200 tests pass (41 new in this workstream,
  1 subagents test updated for the shared effort table: OpenRouter max → `xhigh`). `test.sh` now points `XDG_DATA_HOME` at a temp dir so worker tests do not
  write sessions into the real home.
- Live run with stored keys (Kimi K3 → GLM-5.3 Coding Plan → resume on Kimi): see
  `docs/qa_evidence/2026-09-17-backend-sessions/NOTES.md`. Model switch kept memory, plan file written with the source file
  untouched, manual compaction kept a user fact, resume produced a recap and kept memory. One bug found and fixed (summary
  prompt made GLM refuse to recall user facts).

## QA checklist

1. Configure kimi, ask a fact, `set_model` to glm-coding (and to openrouter if a key exists), ask for the fact: recalled; no provider HTTP 400 from mixed reasoning fields.
2. `set_model` while a turn runs → `error` with `agent_busy: true`; conversation unchanged.
3. `set_effort` for each preset: `applied` matches the table; next request carries the parameters (inspect with a local mock server).
4. `context` event after each response: `estimated:false` for all three providers; `limit_tokens` = 838,860 for Kimi at defaults.
5. Force auto-compaction with `context_window: 8192, compact_threshold: 0.5` and a few tool-heavy turns: `compaction_started {reason:auto}` appears between steps, never between a tool call and its result; the next request succeeds.
6. `compact {focus}` twice in a row: second summary includes the first; cancel during compaction does not corrupt the conversation.
7. Agent writes a file in turn 1 and 2; `rewind {turn:1, restore:"files"}` restores; edit the file manually then rewind → reported under `conflicts`, file untouched; `restore:"conversation"` returns `prompt` and the next turn sees the rewind note.
8. `rewind` to a turn from before a compaction: conversation restored from the snapshot.
9. `fork {turn}` → new worker `configure` + `load_state {state}` → `state_loaded.turns` = turn; both panes continue independently.
10. Restart the worker, `sessions` lists the conversation; `resume` → `state_loaded` then `recap` with text and next_action; `recap_request {reason:"away"}` with <3 turns → `skipped: too_few_turns`.
11. `set_mode plan`, ask for a plan: no file changes except the plan file; `write_file` refused if attempted; `plan_written.path` exists under `.relay/plans`; `plan_execute {path, fresh:true}` after editing the file runs the edited plan in build mode.
12. `scan_instructions` on a real project lists CLAUDE.md/AGENTS.md/WARP.md/global files with correct `exists`/`bytes`; configure with `instructions.files` and `max_bytes: 4096` shows `instructions_truncated`.
13. `synthesize_instructions` with two files writes the target and a `.bak` of the previous file.
14. `ask` with `attachments` (file outside the workspace, a directory, a binary file → error before queuing).
15. `suggest next_command` / `next_prompt` return quickly and never stream `delta` events into the agent transcript.
16. Default skills on a real home: user skills win over bundled duplicates; excluded Warp-app skills reported.

## Known gaps

- GUI wiring (workstream E) not done; `agents`/`agents_list` belong to workstream B.
- Subagent integration: `subagents.effort_extra` now uses `presets.apply_effort`; `set_model` updates the subagent
  factory so `inherit` subagents follow the switch; `resume`/`load_state`/`plan_execute fresh` stop subagents and drop
  their pending results. Plan mode does not offer the `agent` tools (subagents could write files). Subagent file writes
  are not checkpointed.
- Checkpoints cover `write_file` only (not `run_command` effects or `set_keybinding` writes). Forked/loaded sessions keep conversation checkpoints but not file pre-images.
- A single turn larger than the limit can only be trimmed, not summarized. Compaction does not re-inject recently read files.
- Only the last 3 pre-compaction snapshots are kept; older turns lose conversation rewind (`conversation: false`).
- No pruning of old sessions or blobs. Session listing capped at 200.
- Conditional instruction rules (`paths`, `globs`, `alwaysApply: false`) are skipped, not loaded on demand. Git-root walk stops at `$HOME`.
- Skill discovery also picks up Warp's bundled Figma MCP skills and bundled `tab-configs`/`change-keybinding`; extend `skills.exclude` if unwanted.
- OpenRouter effort mapping verified from docs and a raw request, not a full live agent run.
