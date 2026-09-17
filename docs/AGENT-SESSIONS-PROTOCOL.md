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

**Routing assist model (2026-09-17):** when an OpenRouter key is stored, `route_assist` uses
`google/gemini-3.5-flash-lite` on OpenRouter (0.6–0.9 s measured) regardless of the pane's model,
and works before the pane's agent is configured. Without that key it falls back to the pane's model
(reasoning models took 2–12 s; send a longer `timeout_ms`).

## 12. Request ledger, todos, completion check, turn limits (v1.2, 2026-09-17)

Implements items 1–5, 7 and 8 of `docs/MEMORY-AND-MULTI-REQUEST-RESEARCH.md` section 6 (owner decisions in its
section 9). Backend: `backend/relay_core/{requests,todos,agent,queue,context,sidecall,session_protocol}.py`;
tests: `tests/test_requests.py`; opt-in live scenarios: `scripts/eval-requests.py`. All changes are additive:
existing events keep their fields and meaning. Deviations from the research sketches are listed in 12.9.

### 12.1 Options

`configure` and `set_agent_options` accept, all optional:

| Field | Type | Default | Meaning |
|---|---|---|---|
| `max_steps` | int 1–500 | 50 | model calls per turn |
| `max_tool_calls` | int 1–2000 | 150 | tool calls per turn |
| `completion_check` | bool | true | end-of-turn re-prompt for open todos (12.5) |
| `audit_requests` | bool | false | flag-only audit side call after each finished turn (12.6) |
| `todo_tool` | bool | true | offer `update_todos` and its prompt rules to the model |

`configured` gains these five fields. `set_agent_options` applies them to the pane's agent at once (limits are
read at every step boundary) and `agent_options` gains them when an agent is configured (without one, only
`max_auto_turns`/`wakeups` as before). Invalid values → `error`, nothing changed. Subagents are not affected:
they keep their definition's `max_steps`, get `max(24, 3 × max_steps)` tool calls and no ledger or todos.

### 12.2 Turn limits (G1)

When a turn reaches `max_steps` model calls, or makes more than `max_tool_calls` tool calls (the over-budget calls
get an error result and the model gets one more call to answer), the worker emits
`status {text}` and ends the turn with

`done {turn_id, stop_reason: "limit", text, limit: {which: "steps"|"tool_calls", steps, max_steps, tool_calls, max_tool_calls}, open_items}`

This is a `done`, not an `error`: `agent_finished {outcome: "done", stop_reason: "limit"}` follows and the queue is
**not** paused. `turn_summary` also carries `stop_reason: "limit"`. The request stays `open` in the ledger and the
model gets a note that the turn stopped at the limit. The GUI can offer "Continue" (send an ordinary `ask`).
There is no "Stopped at the model-step limit" `error` any more.

### 12.3 Request ledger

Every accepted prompt gets a ledger entry **when it is submitted**, before it is queued or delivered:
`ask` (any `when`), `queue_steer` upgrades (same entry), requeued steers (same entry), Relay-origin prompts such as
subagent wake-ups (`source: "relay"`, `requires_completion: false`), and `plan_execute`. Ids are `R1`, `R2`, …
per conversation (they continue after resume/fork; `reset`/new session starts at `R1`).

Events that gain `ledger_id` (string, or null when tracking is off): `queued`, `steer_returned`.
`steer_delivered` gains `ledger_ids` (list, parallel to `ids`).

**Entry (as listed):**
`{id, text_preview (≤200 chars, whitespace collapsed), source: "ask"|"queue"|"interrupt"|"steer"|"relay", origin: "user"|"relay", requires_completion, status, reason, turn_id, turn, queue_item, delivered, handled, todo_ids: [...], attachments: [paths], audit: [{turn_id, quote}], created, updated}`

- `source` is the `when` the prompt was sent with (`now` → `ask`; an idle `steer` that was queued stays `steer`).
- `turn_id`: the turn (queue item id) that last received it; `turn`: that turn's checkpoint number; `queue_item`: the
  queue item id of its latest submission (matches `queued.id`/`queue_changed.items[].id`).
- `delivered`: it reached the model at least once. `handled`: the turn that last received it ended normally.
- `attachments`: paths only (contents are not stored in the ledger).

**Statuses:** `open` (not finished: never started, or its turn was cancelled, failed or hit the limit),
`in_progress` (its turn is running), `done`, `cancelled` (the model cancelled every linked todo; `reason` = notes),
`cancelled_by_user`, `blocked`, `deferred` (from linked todos; `reason` = notes).
Rules: delivery → `in_progress`. A turn that ends with `done` (not at the limit) marks its requests `done`, unless a
linked todo is still pending/in progress (→ `open`). Linked todos override: all completed → `done`; all cancelled
→ `cancelled`; any blocked → `blocked`; else any deferred → `deferred`. Cancel, error and limit → `open`.
`queue_remove`, `queue_clear` and prompts dropped by `cancel` → `cancelled_by_user`. A status set by the user
(`request_set` done/cancelled_by_user) is never changed by the worker until the user sets `open` or re-asks.

**Event** `requests {id?, items: [entry…] (the newest 200), total, open, counts: {status: n}}`, where `open` counts
`open` + `in_progress` entries with `requires_completion`. Emitted after **every** ledger change (submission, delivery,
status change, todo update, turn end, audit flags) and after `reset`, `load_state`, `resume` and a conversation
`rewind`; `id` is set only in the reply to the `requests` command. The GUI's "Tasks 3/5" chip is derived from
`requests` and `todos` together (see `docs/ARCHITECTURE.md`, "Tasks UI"); it does not use `counts`.

**Commands:**
- `requests {id?}` → `requests {id, …}`.
- `request_get {id?, ledger_id: "R7"}` → `request {id, item: {entry…, text (verbatim)}}`.
- `request_set {id?, ledger_id, status: "open"|"done"|"cancelled_by_user", reason? (≤500 chars)}` → `requests` event
  (no `id`); the session is saved.
- `request_reask {id?, ledger_id, when?: "queue"(default)|"now"|"steer"|"interrupt"}` → submits the verbatim text
  again with the same ledger entry (status → `open`, `reasked` + 1 in the session data), re-reading its attachment
  paths. Replies are the usual `queued {request_id: <id>, ledger_id}` etc. Refused (`error`) while that request is
  `in_progress` or still queued.
All four answer `error` "Request tracking is off for this agent." for agents without a ledger, and
"Configure a provider and workspace first." before `configure`.

**Other events:** `state_loaded` gains `open_requests`; `sessions` items gain `open_requests` (0 for sessions saved
before this version); `recap` gains `open_items: [{id, status, reason, preview}]` (unfinished user requests, ≤20).
Sessions saved before the ledger existed get one rebuilt from their checkpoint prompts (all `done`).

**Persistence:** the ledger, todo list and last plan path are saved in the session (`requests`, `todos`,
`plan_path`). `fork {turn}` carries requests first delivered up to that turn (renumbered to the fork's turns, queue
links dropped) and the todo list as it was before the next turn. `rewind` (conversation) drops requests first
delivered at or after the turn and restores the todo list as it was when that turn started. Queued prompts that
were never delivered are not part of a fork.

### 12.4 Todos (`update_todos` tool)

Model tool (build and plan mode, main agent only, when `todo_tool` is on):
`update_todos {items: [{id?, text (≤500), status: pending|in_progress|completed|cancelled|deferred|blocked, request_ids?: ["R3"], note? (≤500)}]}`
Each call replaces the whole list (≤50 items). At most one `in_progress`; `cancelled`/`deferred`/`blocked` need a
`note`; unknown request ids are refused. Ids are `T<n>`: a known id is kept, anything else gets a new id. A new todo
without `request_ids` is linked to the request that opened the current turn; a resent todo without them keeps its
links. Invalid calls return `{error}` to the model and change nothing. The tool result is
`{ok: true, items, open}`; `tool_started.preview` is `UPDATE TODOS` plus one line per item.

**Event** `todos {id?, turn_id (null outside a turn), items: [{id, text, status, request_ids, note}], open}` after every
successful update, and after `reset`, `load_state`, `resume` and a conversation `rewind`.
**Command** `todos {id?}` → `todos {id, turn_id: null, …}`.

The system prompt gains the todo rules (one todo per ask when a message has several asks or a message arrives
mid-turn; keep going until each is completed or cancelled/deferred/blocked with a reason).

**Stale reminder (item 7):** when open todos exist and `update_todos` has not been called for 8 model steps in the
turn, the worker adds a short user note before the next model call ("update_todos has not been used for 8 steps
while todos are open: … ignore this if it is current"). No event; no extra model call.

### 12.5 Steers, cancel, interrupt, failure (G2, G3)

- **Steers** reach the model as one user message per step boundary, each steer framed:
  `[Sent by the user while you were working (R7, 14:02). Keep your current task (R5) unless this changes it. Add it to your todos if it is a new ask. Say briefly how you handled it in your final answer.]`
  followed by the program-context note, the attachment blocks and the verbatim text. Steer `attachments` and
  `context` are no longer dropped.
- **Cancel, interrupt and failure** no longer remove the user's prompt, delivered steers or subagent notes from
  the conversation. A half-finished tool-call group is completed with
  `{"error": "Not completed: the turn stopped before this tool call finished. …"}` results, then a note says the
  turn was stopped (or failed) and the request is not finished. `cancelled` and `error` for a turn gain
  `open_items` (open item shape in 12.6). Delivered background-subagent results are not re-delivered.

### 12.6 Completion check and audit

**Completion check (item 5):** when the model answers without tool calls and todos linked to this turn's
requests (or new unlinked todos touched this turn) are still `pending`/`in_progress`, the worker emits

`completion_check {turn_id, open: [open item…], reminder: 1|2, max_reminders: 2}`

appends a user note ("Relay completion check 1/2: before finishing, these are still open: … Do them now, or call
update_todos to mark each one cancelled, deferred or blocked with a reason.") and calls the model again, at most
twice per turn (and never past `max_steps`). The turn then ends normally.

**Open item:** `{kind: "request"|"todo", id, status, preview (≤120 chars), request_ids? (todos only)}`.
`done` gains `open_items: [open item…]` on every turn (empty when nothing is open): unfinished requests of the turn
and the open todos described above. Todos of an earlier interrupted request do not hold a later turn open.

**Audit (item 8, `audit_requests`, off by default):** after a turn ends with `done` (not at the limit), a background
no-tools call receives the turn's user requests (verbatim, ids), the final answer and the todo list, and returns
`{unaddressed: [{request_id, quote}]}`. Model: `route_assist.router_provider()` (the route-assist model, when an
OpenRouter key is stored; output limit 1024 tokens), else the pane's model at low effort. Event, always **after**
`done`/`agent_finished`:

`request_audit {turn_id, model, unaddressed: [{request_id, quote (≤200 chars)}], error?}`

Flags are also stored on the ledger entries (`audit`, last 5) and a `requests` event follows. The worker never
re-prompts on a flag; the GUI shows "may be unaddressed: …". Unknown request ids in the reply are dropped; a reply
without the JSON object gives `unaddressed: []` plus `error`.

### 12.7 Compaction (G5, G7, item 4)

- **Turn starts (G5).** Relay tags the user messages it adds with `relay_kind`: `prompt` (a turn-opening prompt),
  `steer`, `note` (cancel/limit/reminder/subagent notes), `summary`, `carried`; and `relay_requests: ["R3"]` on
  prompts and steers. Only `prompt` messages (and untagged messages from older sessions, except an old summary) count
  as turn starts, so steers can no longer push the turn's original prompt into the summarized part. The provider
  strips every `relay_*` key before a request leaves the machine. Session and fork `messages` keep them (additive).
- **Summarizer input (G7).** User messages are no longer middle-trimmed at 4,000 characters, and when the
  transcript is too long the oldest assistant/tool messages are dropped first. The previous carried block is left
  out (it is rebuilt), and when a previous summary is present the model is told to merge it ("anything you do not
  carry into the new summary is lost").
- **Summary sections:** Requests and intent / Decisions and constraints / Files and code / Errors and fixes / Work
  state (Completed, Active, Blocked) / Next step (quote the latest request).
- **Carried block.** After the summary pair (`[system, summary, ack, …]`) the worker inserts a deterministic user
  message `[Relay state carried across compaction: authoritative, not summarized]` plus an assistant ack, containing:
  every **delivered** ledger request with status (open and in-progress requests verbatim up to
  min(30K tokens, window/12); finished ones capped at 400 characters; handled steers marked "handled: do not act on
  it again"; requests whose text is still in the kept conversation or in the recent-messages section are listed by
  id only), the todo list, the last plan written this session, files written this session, running subagents, and
  **recent user messages verbatim** from the summarized part, newest first, up to min(20K tokens, window/16)
  (messages of open requests already shown in full are skipped). If the block would make the conversation larger
  than what was summarized (tiny windows), it is rebuilt without the recent-messages section.
- `compacted` gains `carried: {requests, open, todos, user_messages, user_message_tokens, block_chars, lean}` when a
  block was inserted. The kept tail now starts at index 5 instead of 3 (checkpoint positions are adjusted).

### 12.8 Event order for one turn (main agent)

1. `requests` (new entry) → `queued {…, ledger_id}` → `queue_changed`
2. `agent_started` → `requests` (entry `in_progress`)
3. per step: `status`, thinking/`delta`, `context`; per tool: `tool_started` → `tool_result`. A successful
   `update_todos` emits `requests` (when linked statuses change) and `todos` between the two. A delivered steer
   emits `steer_delivered` → `requests`.
4. optional `completion_check` (then back to 3), at most twice
5. `requests` (turn end) → `turn_summary` → `done {open_items, stop_reason?}` | `cancelled {open_items}` |
   `error {text, open_items}`
6. `agent_finished {outcome, stop_reason?}` → [`steer_returned {ledger_id}` …] → `queue_changed`
7. later, only with `audit_requests`: `requests` (flags) → `request_audit`

### 12.9 Deviations from the research sketches

- The limit outcome is `done {stop_reason: "limit"}` rather than a new outcome `limit`, so the current GUI keeps the
  queue running without changes. Owner default is 50 steps / 150 tool calls (research suggested 60/150).
- `queued` carries `ledger_id` (the sketch's `request_id` already means the GUI's ask id). Commands take
  `ledger_id`, because `id` is the correlation id everywhere else in this protocol; `request_set` also accepts
  `reason`, and `request_reask` was added.
- Turn-opening prompts are not labelled with their id in the model-visible text (only steers are); new todos
  without `request_ids` link to the turn's opening request instead, and the tool result shows the links.
- Finished requests in the carried block have no "request_get" pointer for the model (it has no such tool); the
  full text stays in the session and is available to the GUI via `request_get`.
- On cancel/failure the interrupted tool group is completed with error results instead of being removed, so the
  model still sees which tools ran.
- Requests have one extra status, `cancelled` (model-cancelled via todos), distinct from `cancelled_by_user`.

## 13. Model roles (v1.3, 2026-09-17)

One configurable model per job. Backend: `backend/relay_core/roles.py` (resolution and defaults), with
call sites in `agent.py`, `subagents.py`, `session_protocol.py`, `observe_protocol.py` and `worker.py`;
tests: `tests/test_roles.py`. Source: `issues/features/needs_qa_llm/2026-09-17-model-roles-and-fast-agent.md` (owner,
2026-09-17). All additive: existing fields keep their meaning, and a worker that gets no `roles` behaves
exactly as before.

### 13.1 Roles

| Role (protocol name) | Used for | Default |
|---|---|---|
| `main` | the pane's own agent | the configured preset (read-only here: set with `configure` / `set_model`) |
| `terminal_use` | driving programs, fixing commands | Flash tier |
| `subagent` | subagents that do not name a model | Main tier (the pane's own model) |
| `switchboard` | Switchboard card threads (stored now, used when the Switchboard lands) | Main tier |
| `fast` | panes that default to the fast agent | Flash tier |
| `summaries` | compaction summaries and recaps | Flash tier |
| `suggestions` | next-command and next-prompt suggestions | Flash tier |
| `chores` | duplicate checks, labels, titles, note scans | Lite tier |
| `audit` | the request audit (12.6) | Lite tier |
| `vision` | image turns on presets without image support | GLM main → `glm-5.3-flash`, otherwise main |
| `route_assist` | the routing assist call (section 11) | `google/gemini-3.5-flash-lite` on OpenRouter when a key is stored, else main |

Side calls by role: compaction summaries and recaps use `summaries`; next-command/next-prompt suggestions
use `suggestions`; the request audit uses `audit`; routing assist uses `route_assist`; instruction
synthesis stays on `main`.

`summaries`, `suggestions` and `audit` were split out of `fast` and `chores` on 2026-09-17 so the roles
modal's Advanced list can name one job per row (owner). Their defaults resolve to the same models as
before, so a worker that gets no `roles` still behaves exactly as it did.

### 13.2 Options

`configure` and `set_agent_options` accept `roles`, an object keyed by role name (`main` is rejected: it is
the pane's own model). Each value is `null`, `{}` or `{"inherit": true}` for "same as the main agent", or:

| Field | Type | Meaning |
|---|---|---|
| `tier` | `main`/`flash`/`lite` | follow a tier (13.7); exclusive with the endpoint fields below |
| `preset` | string | a built-in preset id (`kimi`, `kimi-code`, `glm`, `glm-coding`, `minimax`, `openrouter`, `openai`, `anthropic`, `gemini`) |
| `base_url` + `model` | string | a custom endpoint instead of a preset (both required together) |
| `model` | string | with `preset`: a different model id on that provider |
| `extra` | object | provider params; defaults to the preset's `extra` |
| `effort` | `low`/`medium`/`high`/`max` | mapped as in section 3; omitted means the provider's own default |

Keys never cross the pipe: a role resolves its key through the keystore (environment variable, then the
desktop keyring) for the preset matching its endpoint, and reuses the main agent's in-memory key when it
lands on the main preset. Invalid values → `error`, nothing changed.

`configure` also accepts `agent_role` (default `"main"`): the role this pane's **own** agent runs, used by
panes that default to the fast agent.

### 13.3 Fast-agent defaults by main provider

Superseded by the Flash tier (13.7). `fast` resolves to `TIER_DEFAULTS[<main preset>]["flash"]`, which is
the same model it used to be for every provider that existed before, except GLM: Z.AI now rejects
`thinking.type: "disabled"` on GLM-5.3 and GLM-5.3-Flash
(<https://docs.z.ai/guides/capabilities/thinking>), so the Flash tier sends
`{"thinking": {"type": "enabled"}, "reasoning_effort": "low"}` instead.

### 13.4 Events

`configured` gains `agent_role` (string), `roles` — every role, including `main`, as
`{role, label, model, preset, base_url, effort, source, tier, warning?, note?}` — and `tiers` (13.7). `source` is `main` (follows the main
agent), `configured` (from the `roles` table), `default` (a built-in default above) or `fallback` (a
configured role whose key is missing). No key material appears in any of it.

`model_roles {roles, agent_role, warnings, id?}` is emitted when a role table changes (`set_agent_options`),
after a `set_model` (per-provider defaults are recomputed for the new main model), and after `configure`
**only when `warnings` is non-empty**, so a pane that configured cleanly keeps its old event order.
`warnings` holds one line per role that fell back, e.g.
`Subagent: no stored key for glm; using the main agent.`

`set_agent_options {roles}` replies with `agent_options {…, roles}` (the same table) followed by
`model_roles`. A missing key is never a hard failure: the role falls back to the main agent.

### 13.5 `set_agent_role`

`set_agent_role {role, id?}` switches this pane between the main agent and another role (the fast agent in
the GUI) **keeping the conversation**, like `set_model`. Refused while a turn is running. Replies with
`model_changed {model, preset, context_window, effort, agent_role, warning?}` and `context`. A role that
falls back reports `agent_role: "main"`. `configure` with an unusable `agent_role` reports
`agent_role: "main"` too, plus the warning in `model_roles`.

### 13.6 Notes and deviations

- Subagent model specs accept role names (`fast`, `chores`, …) in addition to preset ids; a user alias of the
  same name still wins. A definition's own `model` still overrides the `subagent` role.
- A pane running a non-main role resolves roles that "follow main" against that pane's model, not the
  configured main preset; the `subagent` role and subagent inheritance keep using the configured main model.
- `set_model` rebases the role defaults on the new model and puts the pane back on `agent_role: "main"`.
- `RELAY_KEYRING=off` (environment) skips the desktop keyring entirely; environment keys still work. Tests
  set it so no test run can reach a real keyring.
- Not implemented on purpose (owner: "later"): routing between the main and fast agent by estimated task
  difficulty.

### 13.7 Main / Flash / Lite tiers (v1.4, 2026-09-17)

Eight roles were too many knobs for one screen, so the roles modal shows **three** models — Main, Flash and
Lite — and every role follows one of them. Source: owner, 2026-09-17 ("lets have main, flash, and lite
presets … then advanced options, which would then reveal the specific actions"). Backend:
`presets.TIER_DEFAULTS` (the per-provider table) and `roles.RoleResolver._tier` (resolution); tests:
`tests/test_presets.py` and `tests/test_roles.py`. Additive: a worker that receives no `tiers` and no
`"tier"` in `roles` resolves exactly as v1.3 did.

| Tier | Used for | Where it comes from |
|---|---|---|
| `main` | agent turns, subagents, Switchboard threads | the pane's own model (`configure` / `set_model`) |
| `flash` | terminal use, fast panes, summaries, suggestions | `TIER_DEFAULTS[<main preset>]["flash"]` |
| `lite` | chores and the request audit | `TIER_DEFAULTS[<main preset>]["lite"]` |

**Options.** `configure` and `set_agent_options` accept `tiers`, an object keyed by tier name. `main` is
rejected — it is the pane's own model. Each value is `null` (restore the provider's default) or
`{preset?, base_url?, model?, extra?, effort?}` with the same meaning as a `roles` entry. `roles.<name>`
additionally accepts `{"tier": "main"|"flash"|"lite", "effort"?}`, which is exclusive with
`preset`/`base_url`/`model`/`extra`; giving both is an error.

**Fallback.** A tier whose provider has no stored key steps one tier towards Main — Lite → Flash → Main —
and the Main tier is the pane's own model, so resolution never hard-fails. The step-down is expected, not a
misconfiguration, so it appears as `note` on the tier and the role (`"No stored key for the Lite model;
using Flash."`) and **not** in `model_roles.warnings`; `warnings` stays reserved for a role the user pinned
explicitly whose key is missing.

**Events.** `configured` and `model_roles` gain `tiers`:
`{tier: {tier, label, model, preset, base_url, effort, source, using?, note?}}`, where `source` is
`default` or `configured` and `using` is the tier actually serving it after any step-down. Each role in
`roles` gains `tier` (the tier it came from, or `null` for `vision` / `route_assist`) and an optional
`note`. No key material appears in any of it.

**Defaults per provider** (verified against each provider's own documentation on 2026-09-17; the doc URL
sits next to the entry in `backend/relay_core/presets.py`):

| Default provider | Main | Flash | Lite |
|---|---|---|---|
| `glm`, `glm-coding` | `glm-5.3` | `glm-5.3-flash` (`reasoning_effort: low`) | `google/gemini-3.8-flash` on OpenRouter |
| `kimi` | `kimi-k3` | `kimi-k2.7-code-highspeed` | `google/gemini-3.8-flash` on OpenRouter |
| `kimi-code` | `k3` | `kimi-for-coding-highspeed` | `google/gemini-3.8-flash` on OpenRouter |
| `openrouter` | `deepseek/deepseek-v4.1-flash` | `deepseek/deepseek-v4.1-flash` | `google/gemini-3.5-flash-lite` |
| `minimax` | `MiniMax-M3` | `MiniMax-M2.7-highspeed` | `google/gemini-3.8-flash` on OpenRouter |
| `anthropic` | `claude-opus-5` | `claude-sonnet-5` | `claude-haiku-4-5` |
| `openai` | `gpt-6-astra` | `gpt-5.6-terra` | `gpt-5.6-luna` |
| `gemini` | `gemini-3.1-pro-preview` | `gemini-3.8-flash` | `gemini-3.5-flash-lite` |
| custom / unknown endpoint | the pane's model | the pane's model | the pane's model |

`route_assist` is deliberately **not** tiered: it keeps `google/gemini-3.5-flash-lite` on OpenRouter, because
the routing budget is under a second and that model measured 0.5–0.6 s against 2.3–4.9 s for Gemini 3.8
Flash. Changing the Lite tier must not move it, so the GUI shows it as its own pinned row with that reason.

### 13.8 Key management commands (v1.4, 2026-09-17)

The keys modal needs three things the protocol did not have. All are additive.

| Message | Reply | Meaning |
|---|---|---|
| `remove_key {preset, id?}` | `key_removed {preset, removed: bool}` | delete the keyring entry; `removed: false` when there was none (or `RELAY_KEYRING=off`) |
| `test_key {preset, id?}` | `key_tested {preset, ok, model, elapsed_ms, error?, reply_chars?}` | one minimal call (two-word prompt, no tools, 256 output tokens) that answers "does this key reach this endpoint" |
| `import_agent_tools {id?}` | `agent_tools_imported {imported: [{preset, name, model}], skipped: [string]}` | copy an API key out of `~/.claude/settings.json` (`env.ANTHROPIC_API_KEY`) or `~/.codex/auth.json` (`OPENAI_API_KEY`) |

`test_key` runs on a background thread and emits exactly one event. The key is read from the keystore
inside the worker and never crosses the pipe in either direction; `error` carries the HTTP status only,
because provider error bodies can quote the submitted request (`provider.ProviderError` already strips
them). An OAuth login is not an API key and is never imported: Claude Code and Codex both sign in with
OAuth by default, and those tokens do not work on the OpenAI-compatible endpoints Relay talks to.

The `presets` event gains, per preset, `group` (`subscription` / `aggregator` / `payg`), `key_url` (where
the user gets a key), `note`, and `key_source` (`env` / `keyring` / `""`), so the modal can show
"From RELAY_OPENROUTER_API_KEY" and refuse to offer Remove for something it cannot remove. The event also
gains `tier_defaults` (13.7) and `role_actions` — the Advanced list, one row per job — so the GUI never
keeps a second copy of the backend's tables.
