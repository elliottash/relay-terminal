# Agent sessions, planning, subagents and suggestions: worker protocol (v1, 2026-09-17)

Contract between the GUI (`src/main.cpp`) and the per-pane worker (`backend/worker.py`).
Additive to the existing protocol (route, configure, ask, cancel, queue_*, reset, presets,
store_key, import_warp, keybindings). All messages are one JSON object per line.
Unknown fields are ignored. Errors use the existing `error` event.

Owner decisions this implements are recorded in
`issues/features/2026-09-17-agent-sessions-planning-subagents.md`.

## 1. Configure additions

`configure` accepts, all optional:

| Field | Type | Meaning |
|---|---|---|
| `preset` | string | already exists; used to look up defaults below |
| `context_window` | int tokens | model window; GUI sends the preset value; backend falls back to its preset table |
| `effort` | `low\|medium\|high\|max` | reasoning effort, mapped per provider (section 3) |
| `compact_threshold` | float 0.5–0.98 | auto-compact when used/window reaches this; default 0.90 unless research changes it |
| `session_dir` | abs path | where sessions and checkpoints are stored (default `~/.local/share/relay/sessions/<workspace-hash>/`) |
| `plans_dir` | abs path | default `<workspace>/.relay/plans` |
| `instructions` | `{"files": [abs paths], "project_auto": true}` | instruction files to load; `project_auto` also loads project files found in the workspace (AGENTS.md, CLAUDE.md, WARP.md, ... per the scan table) |
| `agents` | `{"dirs": [abs paths] or omitted}` | agent definition directories; omitted = all known locations (section 7) |

`configured` event gains: `context_window`, `effort`, `mode`, `instructions` (list of loaded paths), `agents` (count), `session_id`.

## 2. Model and effort without losing the conversation

- `set_model {preset, base_url, model, extra, max_tokens, context_window, use_stored_key, api_key?}` → swaps the provider between turns (refused while a turn runs: error with `agent_busy`). Event `model_changed {model, preset, context_window}`.
- `set_effort {effort}` → event `effort_changed {effort, applied: {...provider params}}`.

## 3. Effort mapping

| Relay effort | Kimi K3 | GLM-5.3 (Z.AI) | OpenRouter DeepSeek |
|---|---|---|---|
| low | `reasoning_effort: low` | `thinking: enabled`, `reasoning_effort: low` | `reasoning: {effort: low}` |
| medium | `high` | `high` | `medium` |
| high | `high` | `high` | `high` |
| max | `max` | `max` | `high` (or `xhigh` if supported) |

Verify against provider docs before shipping; keep the table in `backend/relay_core/presets.py`.

## 4. Context and compaction

- After every model response the worker emits `context {used_tokens, window, percent, threshold, estimated: bool}` (provider `usage` when present, else an estimate).
- `context` message → same event on demand.
- Auto-compaction when `percent >= threshold` at a step boundary (never between a tool call and its results): emits `compaction_started {reason: "auto"|"manual"}` then `compacted {before_tokens, after_tokens, summary_chars}`. Order: drop/trim old tool outputs first, then summarize older turns with a no-tools model call, keeping the system prompt, instructions, the last N turns and the current task.
- `compact {focus?: string}` → manual compaction.

## 5. Checkpoints, rewind, fork, sessions, recaps

- A checkpoint is recorded at the start of each user turn: `{turn, prompt_preview, time, message_index}`. Before any agent file write, the file's previous bytes (or "absent") are saved under the session's checkpoint store, keyed by turn.
- `checkpoints` → `checkpoints {items: [{turn, prompt_preview, time, files: [paths]}]}`.
- `rewind {turn, restore: "conversation"|"files"|"both"}` → restores; files changed since (hash mismatch) are skipped and reported. Event `rewound {turn, restored_files: [...], conflicts: [...], note}`. Shell side effects are never undone; the note says so.
- `fork {turn?}` → `fork_state {state}` where `state` is an opaque JSON object (messages up to `turn`, model, effort, mode, instructions). GUI starts a new pane and sends `load_state {state}` → `state_loaded {session_id, turns}`.
- Sessions auto-save after every turn to `session_dir/<session_id>.json` (title = first prompt preview, updated time, model, turns).
- `sessions` → `sessions {items: [{id, title, updated, turns, model}]}`; `resume {id}` → `state_loaded`, followed by a `recap {text}` event.
- **Recap (owner: "claude style recaps", the session-return kind):** when a session is resumed, or when the pane's window regains focus after the agent finished work while the user was away (GUI sends `recap_request`), the worker produces a short summary of what happened (goal, what was done, current state, next step) with a no-tools model call and emits `recap {text, turns_covered}`.

## 6. Plan mode (Warp-style)

- `set_mode {mode: "build"|"plan"}` → `mode_changed {mode}`.
- Plan mode: run_command, read_file, list_directory, load_skill, read_skill_file stay available for investigation; write_file and set_keybinding are removed from the tool list; the system prompt says to investigate without changing anything and to finish by calling `write_plan`.
- Tool `write_plan {title, content}` (plan mode only) writes `plans_dir/<YYYY-MM-DD-HHMM>-<slug>.md` and emits `plan_written {path, title}`. The GUI opens it in an editable pane.
- The GUI executes a plan by sending `set_mode build` then `ask` with text referencing the plan path; "execute in fresh context" sends `reset` first.

## 7. Instructions and agent definitions

- `scan_instructions {workspace}` → `instructions_found {items: [{path, tool, scope: "global"|"project", bytes, exists}]}` covering the conventions table in `docs/INTAKE-CLARIFICATION-RESEARCH.md`.
- `synthesize_instructions {files: [...], target}` → runs a no-tools model call that merges the files into one relay.md and writes `target` (default `~/.config/relay/relay.md`) → `instructions_synthesized {path, bytes}`.
- Loaded instructions go into the system prompt, each labelled with its path, lower priority than the user's request, with a size cap.
- Agent definitions load from every known location by default: `.relay/agents`, `~/.config/relay/agents`, `.claude/agents`, `~/.claude/agents`, opencode's `.opencode/agent(s)` and `~/.config/opencode/agent(s)`, plus any others in the research table. Later sources do not override earlier ones with the same name; duplicates are reported.
- `agents_list` → `agents {items: [{name, description, source, model, tools}]}`.

## 8. Subagents

- Main-agent tool `agent {description, prompt, subagent_type, background: bool, model?, effort?}`; `agent_message {id, text}`; `agent_wait {id?}`. Several `agent` calls in one response run concurrently (max 4). Subagents cannot spawn subagents.
- Events: `subagent_started {id, type, description, background, model}`, `subagent_progress {id, status: "running"|"waiting"|"done"|"failed"|"stopped", tools, tokens, elapsed_ms, last_activity}`, `subagent_finished {id, outcome, summary}`.
- `agent_subscribe {id, on: bool}` → while on, the worker also sends `subagent_event {id, event: {...}}` wrapping that subagent's delta/tool_started/tool_output/tool_result events.
- `agent_message {id, text}` (user → subagent), `agent_stop {id | "all"}`.
- Background completion: the result is delivered to the main agent before its next model call; if the main agent is idle, the worker enqueues a main turn "Background agent <id> finished: <summary>" (owner decision 4).

## 9. Suggestions

- `suggest {kind: "next_command", command, exit_status, cwd, output_tail?}` → `suggestion {kind, id, text, reason}` (AI-suggested next shell command; opt-in setting, off by default in the GUI).
- `suggest {kind: "next_prompt"}` after an agent turn → `suggestion {kind: "next_prompt", text}` (Claude-style prompt suggestion).
- Ghost-text history autosuggestions are GUI-only (no worker call).

## 10. Attachments

`ask` accepts `attachments: [{path}]` (from `@file` in the composer). The worker reads each (text, capped, inside or outside the workspace if the user picked it) and prepends a labelled block to that user turn.

## 11. Routing assist, thinking, tool outputs, skills (v1.1, 2026-09-17)

Owner decisions: model-assisted routing is allowed; thinking and tool calls must be easy to
observe; skills can be refined and imported with review. Scratchpad design is pending research.

**Routing assist.** `route` results gain `needs_assist: bool` (local rules could not decide:
command-like English words such as go, install, make, find, open, test, build, start, time, plus
sentence signals) and `assist_reason`. The GUI then sends `route_assist {id, text, cwd, mode}` →
`route_assisted {id, route: "shell"|"agent", confidence: 0..1, reason, elapsed_ms}` using the
pane's configured model with no tools, low effort and a tiny max_tokens; errors or a 2 s timeout
return `route_assisted {id, route: null, error}`. The GUI must never block typing on it.

**Thinking.** The worker streams reasoning as `thinking_delta {turn_id, text}` (from provider
reasoning fields) and ends with `thinking_done {turn_id, elapsed_ms, chars}`. `delta` stays
answer text only.

**Turn and tool summaries.** Each agent turn has a `turn_id` included in `agent_started`,
`tool_started`, `tool_result` and `done`. At turn end the worker emits
`turn_summary {turn_id, elapsed_ms, thinking_ms, tools: [{call_id, name, preview, ok, exit_code?}]}`.
`tool_output_get {turn_id, call_id}` → `tool_output {turn_id, call_id, name, preview, result}`
(the worker keeps results for the last 50 turns). `turn_transcript_get {turn_id}` →
`turn_transcript {turn_id, items: [...]}` in the same shape as `subagent_transcript`.

**Skills.** `skills_list` → `skills {items: [{name, description, path, source, excluded, refined_from?}]}`.
`refine_skills {names: [...], target_dir?}` (default `~/.config/relay/skills`) writes refined copies
without touching originals → `skills_refined {items: [{name, path, from}]}`.
`import_skills_preview {url, ref?}` clones into a temporary dir and pins the commit →
`skills_import_preview {url, commit, items: [{name, description, path, files}]}`;
`import_skills_confirm {url, commit, names}` copies the chosen skills to
`~/.local/share/relay/skill-imports/<repo>@<commit>/` and enables them → `skills_imported {items}`.
No automatic updates; `skills_check_updates {url}` → `skills_updates {url, current, latest}`.

### 11.1 Backend implementation notes and deviations (2026-09-17)

Implemented in `backend/relay_core/{router,route_assist,provider,agent,queue,skills,skill_manage,observe_protocol}.py`;
tests in `tests/test_routing_thinking_skills.py`; live evidence in
`docs/qa_evidence/2026-09-17-routing-thinking-skills/`. Where this differs from the text above:

- **Routing list and signals.** `router.ENGLISH_COMMANDS` is an inclusive list of commands that are English words;
  a word only matters when it also resolves (PATH, builtin, alias or function). Signals: articles, pronouns,
  question words, please/thanks (weighted), connectives (to, and, for, is, ...), a trailing `?`, trailing sentence
  punctuation, more than 4 plain words with no path, `go <word>` that is not a go subcommand, and a bare non-file
  word after file-taking commands (`install ripgrep`, `open settings`). Flags, operators, `$`, globs, `=` and
  quotes mean no assist. Score ≥ 2 sets `needs_assist`. The local guess is `agent`, except for literal-text commands
  (echo, printf, say, ...), where it stays `shell`. Phrases the older natural-language rule already routed to the
  agent ("make the tests pass", "find the config") also get `needs_assist: true` when the first word is a real
  English-word command; their route stays `agent`.
- **PATH scan.** Executables are scanned per PATH directory and cached by (device, inode, mtime), so an install or
  removal shows up on the next `route`; a cache miss still falls back to a direct lookup (a `chmod +x` does not
  change the directory mtime).
- **`route_assist` output limit is 256 tokens, not ~20.** Kimi K3 cannot turn thinking off and GLM/DeepSeek reason
  first; live, 20 tokens truncated both Kimi and GLM, 64 truncated Kimi, 128 truncated OpenRouter DeepSeek. The JSON
  reply itself is ~20 tokens.
- **`route_assist` accepts optional `timeout_ms`** (100–15000, default 2000 as specified). Live on 2026-09-17 the
  2 s default timed out for 6/6 Kimi and 4/6 GLM Coding calls (Kimi answers took 2.9–8.9 s, GLM 1.7–11.7 s); with
  15 s all 12 answered correctly. The GUI should keep its local guess and may send a longer `timeout_ms`.
  Without a configured agent the reply is `route_assisted {route: null, error: "not_configured"}`.
- **`tool_output` name collision.** Streaming command output already uses `tool_output {text}`. The stored reply to
  `tool_output_get` is `tool_output {stored: true, turn_id, call_id, name, preview, result, ok, exit_code?}` with no
  `text`; the GUI must branch on `stored`.
- **Turn ids.** `turn_id` is the queue item id (same as `agent_started.id`); turns started outside the queue
  (subagents) get a generated id. `turn_id` is also on `error` and `cancelled`, and `call_id` on `tool_started` and
  `tool_result`.
- **`turn_summary`** is emitted for every outcome, immediately *before* the terminal `done`/`error`/`cancelled`
  (so those stay the last event of a turn). Extra fields: `outcome`, `thinking_chars`. `tools[].preview` is the last
  non-empty line of the tool preview (the command or path), at most 160 characters.
- **`thinking_done.elapsed_ms`** counts from when the request was sent, not from the first reasoning chunk: GLM
  buffers reasoning and delivers it in one burst just before the answer (live: 0 ms vs 9.4 s). If a stream stops
  mid-reasoning (error or cancel), the worker still sends `thinking_done {elapsed_ms: 0, chars: 0}`.
  `reasoning_details` text/summary items are used only when neither `reasoning_content` nor `reasoning` is present.
  Subscribed subagents also forward `thinking_delta`, `thinking_done` and `turn_summary` in `subagent_event`.
- **`turn_transcript`** is `{turn_id, outcome, running, items}`; each item has the `subagent_transcript` message shape
  `{role, content (≤ 8000 chars), tool_calls?: [names]}` plus `tool_call_id` on tool results. Items are recorded as
  the turn runs, so a cancelled or failed turn keeps its transcript even though the conversation rolls it back.
- **Skills search order** (default directories only; an explicit `configure.skills.dirs` list is used as given):
  `~/.config/relay/skills` (refined copies, `XDG_CONFIG_HOME` aware) first, then the existing locations, then
  `~/.local/share/relay/skill-imports/<repo>@<commit>/` (newest first, `XDG_DATA_HOME` aware). A clash is reported
  in `configured.skills_skipped` as "refined copy in … overrides …".
- **`skills_list`** items also carry `shadowed_by` (the winning SKILL.md) for duplicates; the event adds `skipped`.
- **`refine_skills`** writes `<target_dir>/<name>/SKILL.md` and copies the skill's other files (no symlinks, no dot
  files). The model's body and description are used; `name` and all other frontmatter keys come from the original,
  plus `refined_from` (the original path; refining a refined copy keeps pointing at the original). A reply without
  frontmatter or instructions is rejected. Event: `skills_refined {items, errors: [{name, error}], reloaded}`;
  `reloaded` means the idle agent picked up the new index and system prompt.
- **Import.** URLs: `https://`, `ssh://` and `git@host:path` (no credentials in https URLs); `file://` only when
  `skill_manage.ALLOW_FILE_URLS` is set (tests). Instead of `git clone --depth 1`, the worker runs `git init` plus
  `git fetch --depth 1 origin <ref|HEAD>` and checks out `FETCH_HEAD`, so `ref` may be a branch, tag or commit. Git
  runs with only the https/ssh(/file) protocols allowed, hooks disabled, `core.symlinks=false`, no prompts and a
  120 s timeout. `skills_import_preview` adds `ref` and `skipped` (symlinked folders, missing descriptions,
  duplicates); `items[].path` is relative to the repository. The preview clone stays in a temp directory until
  confirmed or the worker exits; `import_skills_confirm` re-fetches the pinned commit if needed. Copies go to
  `skill-imports/<repo>@<full commit>/<name>/` with a `.relay-import.json` manifest; `skills_imported` adds `url`,
  `commit`, `dir`, `reloaded`. `skills_check_updates {url, ref?}` → `skills_updates {url, ref, current, latest,
  update_available}`, where `current` is the newest imported commit for that URL.
