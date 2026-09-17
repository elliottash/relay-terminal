# Agent-session UX: Warp, opencode, Claude Code, Codex, and what Relay should build

Research date: 2026-09-17. Scope: session lifecycle, planning, model controls, subagents, input
affordances, memory. Recommendations for Relay are in Part II. Nothing here is implemented.

**Citation keys** (every table cell cites one of these; *unverified* marks gaps):
- **CC:x**: `https://code.claude.com/docs/en/x` (docs fetched 2026-09-17; CLI v2.1.274 installed locally).
- **W:x**: `https://docs.warp.dev/x`.
- **OC:x**: `github.com/sst/opencode` at `88c6c7abc7f3`. `oc/` = `packages/opencode/src/`, `tui/` = `packages/tui/src/`, `core/` = `packages/core/src/`. **OCD:x**: `https://opencode.ai/docs/x/`.
- **CX:x**: `github.com/openai/codex` at `e269f2164cbb`, path under `codex-rs/`. **CXD:x**: `https://learn.chatgpt.com/docs/x` (developers.openai.com/codex now redirects there). `[help]` = `codex 0.154.0 --help`.
- **R:x**: this repo.

**Relay baseline** (verified in source): one conversation per pane (`R:backend/relay_core/agent.py`);
`reset` clears it; a single dispatcher with now/queue/interrupt (`R:backend/relay_core/queue.py`);
tools run without approval; `usage` events are emitted but ignored (`R:backend/relay_core/provider.py:~186`);
no `stream_options.include_usage`. **Switching model sends `configure`, which builds a new `Agent`
and drops the conversation** ("Switching model. This starts a new conversation.", `R:src/main.cpp:1584`).
`configure`/`reset` are refused while busy (`R:backend/worker.py`). Composer: Ctrl+I toggles
terminal/agent, Esc focuses native terminal, Up/Down on first/last line walk history (`R:README.md`).
`ToolPane` already hosts Explorer/Preview panes (`R:src/main.cpp:1986`).

# Part I: Comparison

## 1. Session lifecycle

| Capability | Warp | opencode | Claude Code | Codex CLI |
|---|---|---|---|---|
| New / clear | `/new`, `/agent`, alias `/clear` (W:agents/capabilities/slash-commands); auto new convo after 3h idle or a shell command (W:agents/local-agents/interacting-with-agents/) | `/new` alias `/clear`, `<leader>n` (OC:tui/app.tsx:588; tui/config/keybind.ts:89) | `/clear` aliases `/reset` `/new`; optional name labels the old session (CC:commands) | `/new` opens a checkout picker; `/clear` clears screen + new chat (CX:tui/src/slash_command.rs:91,100; tui/src/chatwidget/slash_dispatch.rs:185) |
| Resume / history | Conversation panel Active/Past, `Ctrl+Shift+H`, `/conversations` (W:agents/local-agents/interacting-with-agents/) | `/sessions` (`/resume`), `<leader>l`; root sessions only; `-c`, `-s <id>` (OC:tui/component/dialog-session-list.tsx:190; oc/cli/cmd/tui.ts:86) | `--continue`, `--resume`, `/resume` picker with preview/rename/branch filter; JSONL at `~/.claude/projects/…`, 30-day retention; "Resume from summary" for idle large sessions (CC:sessions) | `codex resume [--last\|--all]`, `/resume` picker with transcript preview; rollouts `~/.codex/sessions/YYYY/MM/DD/*.jsonl` (CX:tui/src/resume_picker.rs; rollout/src/recorder.rs:1700) |
| Fork | `/fork [prompt]` (new pane or current), `/fork-from` list, "Fork conversation from here" on any block, `/fork-and-compact`; fork keeps model and profile (W:agents/local-agents/interacting-with-agents/conversation-forking/) | `/fork` dialog: "Full session" or any user message; forks *before* that message and pre-fills it; `--fork` (OC:oc/session/session.ts:691-730; tui/routes/session/dialog-fork-from-timeline.tsx:22-75) | `/branch` (switch into copy; grants carry over), `--fork-session`, `/fork` = copy into a background session, `/subtask` = forked subagent (CC:sessions; CC:agent-view) | `codex fork`, `/fork`; `/side` `/btw` ephemeral fork; Esc Esc backtrack forks from an earlier prompt (CX:tui/src/slash_command.rs:101,136; tui/src/app_backtrack.rs:1-22) |
| Rewind / undo | `/rewind`: restores code **and** conversation, keeps a copy of the original; does not cover manual or shell edits (W:agents/capabilities/slash-commands,203) | `/undo` `/redo` (`<leader>u/r`) = revert/unrevert; files restored from a separate git dir snapshotted per step; messages hidden, deleted on next prompt; needs git (OC:oc/session/revert.ts:38-124; oc/snapshot/index.ts:71; OCD:tui) | `Esc Esc` or `/rewind`: per-prompt checkpoints; Restore code+conversation / conversation / code / Summarize from here / up to here; tracks only Claude's file-edit tools, not Bash, not subagent edits; 100 checkpoints (CC:checkpointing) | Esc Esc backtrack = `thread/revert`, **history only** ("does not revert local file changes"); `/undo` + ghost commits removed (CX:app-server-protocol/src/protocol/v2/thread.rs:1257; features/src/lib.rs:942) |
| Compact | `/compact`, `/compact-and <prompt>`; auto-summarize when the window is exceeded (W:agents/capabilities/slash-commands) | `/compact` (`/summarize`); auto when tokens ≥ input limit − min(20k, max output); template Objective / Details / Work State / Next Move / Files; recent tail kept verbatim (2k–15k tok); prune opt-in (OC:oc/session/overflow.ts; oc/session/compaction.ts:28-120; core/session/compaction.ts:16-55) | `/compact [focus]`; re-injects CLAUDE.md, plan, 5 recent files; auto threshold = context limit (~967K on 1M models), `/autocompact` (CC:context-window; CC:model-config) | `/compact`; auto at min(`model_auto_compact_token_limit`, 90% window), pre-turn and mid-turn; handoff prompt `prompts/templates/compact/prompt.md` (CX:core/src/compact.rs:121; protocol/src/openai_models.rs:521; core/src/session/turn.rs:603) |
| Export / share | `/export-to-clipboard`, `/export-to-file`; cloud share links; live session sharing (W:agents/local-agents/cloud-conversations/; W:agents/local-agents/session-sharing/) | `/export` with options, `/copy`, `/share` `/unshare`, share = manual/auto/disabled (OC:tui/routes/session/index.tsx:466-608; core/v1/config/config.ts:57) | `/export [file]` (CC:sessions) | `/export` Markdown, `/copy`; no share link found (*unverified*) (CX:tui/src/slash_command.rs:105-106) |

## 2. Planning

| Capability | Warp | opencode | Claude Code | Codex CLI |
|---|---|---|---|---|
| Enter plan mode | `/plan [prompt]` or plain language (W:agents/capabilities/planning/) | `plan` primary agent; **Tab/Shift+Tab cycle agents** (OC:oc/agent/agent.ts:141-181; tui/config/keybind.ts:129) | **Shift+Tab** cycles Manual → acceptEdits → plan (→ bypass/auto if enabled); `/plan [desc]` (CC:permission-modes) | **Shift+Tab** cycles Default/Plan collaboration modes; `/plan [prompt]` (CX:tui/src/keymap.rs:2375; tui/src/slash_command.rs:131) |
| Read-only enforcement | Permission "Create plans"; no hard read-only mode documented (W:agents/capabilities/agent-profiles-permissions/) | `edit` denied except `.opencode/plans/*.md`; bash **not** restricted, prompt-only (docs claim "ask") (OC:oc/agent/agent.ts:156-181; oc/session/prompt/plan.txt) | Edits blocked until approval; commands classifier-reviewed or prompted (CC:permission-modes) | Prompt-level: "only non-mutating actions"; plan in `<proposed_plan>` (CX:collaboration-mode-templates/templates/plan.md:96-128) |
| Approve then execute | Plan doc in rich editor with versions; user prompts "implement phase 1"; orchestration plans need approval (W:agents/capabilities/planning/; W:platform/orchestration/) | Experimental `plan_exit` tool asks Yes/No, injects "plan approved… execute" with agent=build (OC:oc/tool/plan.ts:15-79; oc/session/reminders.ts:51-89) | `ExitPlanMode` card: Yes + auto mode / Yes, manually approve edits / No, keep planning; Ctrl+G edit plan; optional "approve and clear context" (CC:permission-modes; CC:hooks) | "Implement this plan?": Yes / Yes, clear context and implement / No, stay in Plan (CX:tui/src/chatwidget/plan_implementation.rs:9-27) |
| Todo list | Auto task list, chip bottom-right, ● ✔ ○ ■ (W:agents/capabilities/task-lists/) | `todowrite` only; inline list + sidebar panel (OC:oc/tool/todo.ts; tui/feature-plugins/sidebar/todo.tsx) | `TaskCreate/Update/List`; Ctrl+T toggles checklist (CC:interactive-mode; CC:tools-reference) | `update_plan{explanation?, plan[{step,status}]}`, one in_progress; rendered "• Updated Plan" ✔/□ (CX:core/src/tools/handlers/plan_spec.rs:7-58; tui/src/history_cell/plans.rs:175) |

## 3. Model controls

| Capability | Warp | opencode | Claude Code | Codex CLI |
|---|---|---|---|---|
| Switch mid-session | Toolbelt picker, `/model`, auto routers (W:agents/inference/model-choice/) | `/models`, `<leader>m`, F2 cycles recent (OC:tui/config/keybind.ts:119-125) | `/model` (applies to next request, even mid-turn), Alt+P picker keeps draft (CC:commands; CC:interactive-mode) | `/model` then "Select Reasoning Level"; allowed mid-task (CX:tui/src/chatwidget/model_popups.rs:473) |
| Reasoning effort | Encoded as model variants (`…-low`…`-max`) (W:agents/inference/model-choice/) | **Variants**: Ctrl+T cycles, `/variants`; per-provider mapping to `reasoningEffort`; shown in prompt bar (OC:oc/provider/transform.ts:777-951; tui/component/prompt/index.tsx:1304) | `/effort low…max`; ←/→ in `/model`; `ultrathink` per prompt; Alt+T thinking toggle; shown in header (CC:model-config) | Effort chosen in `/model`; **Alt+, / Alt+.** lower/raise (never silently to Max) (CX:tui/src/chatwidget/reasoning_shortcuts.rs:1-14) |
| Context indicator | Meter hidden <20%, red near limit, breakdown card (W:agents/local-agents/interacting-with-agents/) | Sidebar tokens, % used, $ spent; subagent footer per child (OC:tui/feature-plugins/sidebar/context.tsx:13-46) | `/context` grid; statusline JSON `context_window.used_percentage` (CC:statusline); built-in footer % *unverified* | Footer "N% context left" (CX:tui/src/bottom_pane/footer.rs:1041-1053) |
| Cost | Per-turn credits chip, `/cost`, `/usage` (W:support-and-community/plans-and-billing/credits/) | `$X spent` per session; child costs not rolled up (*unverified*) | `/usage` (`/cost`), per-subagent attribution on paid plans (CC:costs) | Status items `EstimatedThreadCost`, `ThreadCredits` (CX:tui/src/bottom_pane/status_surface_preview.rs:11-42) |

## 4. Subagents and parallel agents

| Aspect | Warp | opencode | Claude Code | Codex CLI |
|---|---|---|---|---|
| Definitions | No user agent files; children inherit parent profile; may use another harness (W:platform/orchestration/) | Markdown `{agent,agents}/**/*.md` in `.opencode/` and `~/.config/opencode/`, or JSON `agent`; fields `description, mode, model, variant, temperature, steps, permission, hidden, color, prompt` (OC:oc/config/agent.ts:11-32; core/v1/config/agent.ts:13-58) | Markdown in `.claude/agents/` (walks up) and `~/.claude/agents/`; frontmatter `name, description, tools, disallowedTools, model, permissionMode, maxTurns, skills, mcpServers, hooks, memory, background, effort, isolation: worktree, color` (CC:sub-agents#supported-frontmatter-fields) | TOML roles in `[agents.<role>]` or `<config>/agents/*.toml` (`description`, `config_file`, `nickname_candidates`); roles can only narrow authority (CX:agent-roles/src/loader.rs:75; core/src/agent/role.rs:1-4) |
| Built-ins | n/a | `general`, `explore` (read-only tools) (OC:oc/agent/agent.ts:182-218) | Explore (read-only, skips CLAUDE.md), Plan, general-purpose (CC:sub-agents#built-in-subagents) | `default`, `explorer`, `worker` (CX:core/src/agent/role.rs:341-398) |
| Delegation tool | `RunAgents`, `SendMessageToAgent`, `WaitForEvents`, `FetchConversation`; approval card "Can I start additional agents?" (W:agents/cli/cloud-and-orchestration/) | `task{description, prompt, subagent_type, task_id?, command?, background?}`; result `<task id state><task_result>…` = last text part of child (OC:oc/tool/task.ts:43-79,200-225) | `Agent` (was Task) `{description, prompt, subagent_type, model, run_in_background, name, isolation}`; returns `agentId`, content, tokens, duration; `SendMessage{to}` resumes (CC:sub-agents; CC:hooks#agent); full model-facing schema *unverified* | V1 `spawn_agent{message, agent_type, fork_context, model, reasoning_effort}`, `send_input{interrupt}`, `wait_agent`, `close_agent`; V2 `spawn_agent{task_name, fork_turns}`, `send_message`, `followup_task`, `list_agents`, `interrupt_agent` (CX:core/src/tools/handlers/multi_agents_spec.rs) |
| Fg / bg | Children always async, coordinated by server mailbox (W:platform/orchestration/) | Foreground by default; parallel via multiple calls in one message; experimental `background`, Ctrl+B backgrounds running tasks (OC:oc/tool/task.ts:97-102,289-296) | Interactive default: all subagents background ("fork mode", v2.1.232+); Ctrl+B backgrounds a running task (CC:sub-agents#run-subagents-in-foreground-or-background) | Children run concurrently; parent keeps working (CX:core/src/agent/control.rs:630) |
| Display / navigation | GUI pill bar above agent view, click pill to switch pane to child; CLI "Agents:" tab bar via Shift+Up (W:platform/orchestration/multi-agent-runs/) | Inline "Agent Task — desc" block with live "↳ Tool" line; click opens child session; `<leader>down` into child, left/right siblings, up to parent; footer "Label (i/N)" (OC:tui/routes/session/index.tsx:2215-2320; tui/routes/session/subagent-footer.tsx) | **Panel below the prompt**: `main` row + one per agent (tree for nested); ↑/↓ select, **Enter opens transcript and lets you send follow-ups**, `x` stops/dismisses, Esc back; `/tasks` list; Ctrl+X Ctrl+K ×2 stops all (CC:sub-agents#observe-and-steer-running-forks; CC:interactive-mode). Exact key that moves focus from prompt into the panel (Down) *unverified* | `/subagents` picker, `/agents` overview, Alt+Left/Right switch threads; rows nickname [role] status (CX:tui/src/multi_agents.rs:33-111) |
| Completion / handoff | Toasts and mailbox for parent only; child states in parent transcript (W:agents/capabilities/agent-notifications/) | Final text returned; background completion injected as synthetic user message (OC:oc/tool/task.ts:227-265) | Only final message returns; background result arrives as notification on a later turn; reports scanned for injection; successful rows vanish, footer "/tasks to see subagents" (CC:sub-agents) | V1 injects `<subagent_notification>` without starting a turn; V2 `InterAgentCommunication` Result (CX:core/src/agent/control.rs:690-722) |
| Isolation | Separate conversations; **no automatic worktree**, children share cwd (W:agents/cli/cloud-and-orchestration/) | Child session with inherited deny rules; `task` and `todowrite` denied (OC:oc/agent/subagent-permissions.ts:14-27) | Fresh context (own prompt + CLAUDE.md + git status); `isolation: worktree`; rewind does not restore subagent edits (CC:sub-agents; CC:checkpointing) | Shared filesystem; worker role told "not alone in the codebase" (CX:core/src/agent/role.rs) |
| Limits | Nesting 1 (multi-level dogfood only) | `subagent_depth` default 1 (OC:oc/tool/task.ts:104-117) | Depth 3, 20 concurrent (CC:sub-agents#concurrent-subagent-limit) | V1 6 threads depth 1; V2 4 incl. root (CX:core/src/config/mod.rs:250-261) |
| Approvals from children | Answer in child view (W:agents/cli/cloud-and-orchestration/) | Child prompts surface in parent view (OC:tui/routes/session/index.tsx:232) | Surface in main session naming the agent (CC:sub-agents) | Can surface from inactive threads; `o` opens requesting thread (CXD:agent-configuration/subagents; CX:tui/src/bottom_pane/approval_overlay.rs) |

**Warp vs Claude Code (analysis).** Warp's orchestration is heavyweight: children are server-backed
"runs" started behind approval cards or `/orchestrate`,
talking through a mailbox, possibly requiring Warp's servers even locally (*unverified*). There is no
cheap model-invoked "delegate, get a summary back" tool. Nesting is one level. Local children share the
cwd with no worktree option, don't appear in Agent Management, don't notify, and cancelling the parent
does not cancel them. You only find them through the pill bar while viewing the parent. Claude Code keeps
delegation cheap (one tool call), isolates context, runs in the background by default, and puts every
running agent in one keyboard list under the prompt where you can inspect, message, or stop it. Warp is
ahead on status chrome (badges, mailbox, desktop alerts), per-run credits, and mixed local/cloud children.

## 5. Commands and input affordances

| Affordance | Warp | opencode | Claude Code | Codex CLI |
|---|---|---|---|---|
| Slash menu | ~50 commands incl. skills as `/{skill}` (W:agents/capabilities/slash-commands) | Palette Ctrl+P + slash list; custom `.opencode/command/*.md` with `$ARGUMENTS`, `agent`, `model`, `subtask` (OC:oc/session/prompt.ts:1370-1455; OCD:commands) | Filtered `/` menu; commands queue while busy except `/model /effort /tasks /usage /status /fast`; skills = commands (CC:commands; CC:skills) | Popup in enum order; per-command "allowed during task" (CX:tui/src/slash_command.rs:12-14,207-267) |
| `@` / `!` / `#` | `@` files, blocks, Drive, plans; `!cmd` forces shell; Ctrl+I toggles (W:agents/local-agents/agent-context/using-to-add-context/) | `@` files and subagents; `!` shell mode (OC:tui/component/prompt/index.tsx:824-858) | `@` files/agents; `!` bash mode output enters context; `#` **removed** v2.0.70 (CC:interactive-mode; CC:changelog) | `@` files, `$` skills/connectors, `!` shell (CX:tui/src/keymap.rs:2366-2389) |
| Interrupt / rewind keys | Ctrl+C stops; Esc exits with double-press (W:agents/local-agents/interacting-with-agents/) | Esc ×2 within 5s interrupts (OC:tui/component/prompt/index.tsx:394-419) | Esc interrupts; Esc Esc on empty input = rewind; Ctrl+R history (CC:interactive-mode; CC:checkpointing) | Esc interrupts; Esc Esc backtrack; Ctrl+R history (CX:tui/src/keymap.rs:1579-1617) |
| While busy | Default **interrupt**; setting to queue; queue panel, `/queue`, pauses on error (W:agents/local-agents/interacting-with-agents/prompt-queueing/) | Messages join the running loop at next step, tagged QUEUED (OC:tui/routes/session/index.tsx:1387-1450) | Queued above input, delivered within the turn after current tool calls; Up pulls back (CC:interactive-mode#queue-messages-while-claude-works) | **Enter steers** into the current turn, **Tab queues** (CX:tui/src/chatwidget/input_flow.rs:25-58) |
| Approvals | Agent decides / Always ask / Always allow / Never per action; regex allow/deny; Ctrl+Shift+I auto-approve (W:agents/capabilities/agent-profiles-permissions/) | allow/ask/deny patterns; Allow once / always (this run) / Reject + message (OC:tui/routes/session/permission.tsx:405-428) | Yes / Yes don't ask again / No (+Tab comment); modes default, acceptEdits, plan, auto, dontAsk, bypassPermissions (CC:permissions; CC:permission-modes) | Yes / Yes for prefix / Yes this session / No continue / No and tell Codex; presets Read Only, Default, Full Access (CX:tui/src/bottom_pane/approval_overlay.rs:835-915; utils/approval-presets/src/lib.rs:28-56) |

## 6. Memory and instructions

| | Warp | opencode | Claude Code | Codex CLI |
|---|---|---|---|---|
| Project | `AGENTS.md` (or `WARP.md`) root + cwd, subdirs best-effort (W:agents/capabilities/rules/) | First of AGENTS.md → CLAUDE.md → CONTEXT.md walking up; nested ones attached on read (OC:oc/session/instruction.ts:64-131) | `CLAUDE.md`, `.claude/CLAUDE.md`, `CLAUDE.local.md`, `@imports`, `.claude/rules/*.md` with `paths:`; **AGENTS.md not read natively** (CC:memory) | `AGENTS.override.md` > `AGENTS.md` > fallbacks per dir, root→cwd, 32 KiB, skipped if untrusted (CX:core/src/agents_md.rs:1-298) |
| User | Global rules in Warp Drive (W:agents/capabilities/rules/) | `~/.config/opencode/AGENTS.md` or `~/.claude/CLAUDE.md` (OC:oc/session/instruction.ts:60) | `~/.claude/CLAUDE.md`, `~/.claude/rules/`; auto memory `MEMORY.md` (CC:memory) | `~/.codex/AGENTS.md`; memories feature off by default (CX:codex-home/src/instructions/mod.rs; features/src/lib.rs:1135) |
| Skills | `.agents/skills`, `.warp/skills`, `.claude/skills`, … and `~/` equivalents (W:agents/capabilities/skills/) | skills as commands (OC:oc/command/index.ts) | `.claude/skills/*/SKILL.md` (CC:skills) | `.agents/skills`, `$CODEX_HOME/skills` (CX:ext/skills/src/host_roots.rs:66-171) |

# Part II: Recommendations for Relay

## Provider facts that constrain the design

| Preset | Effort parameter | Values (default) | Context | Source |
|---|---|---|---|---|
| Kimi K3 | top-level `reasoning_effort`; thinking cannot be disabled; return assistant message (with `reasoning_content`) unchanged | `low`/`high`/`max` (`max`) | 1M; output up to 1,048,576 | https://platform.kimi.ai/docs/guide/kimi-k3-quickstart |
| GLM-5.3 | `thinking:{type:"enabled"}` (only value) + `reasoning_effort` | `low`/`high`/`max` (`max`) | 1M; output 128K | https://docs.z.ai/guides/llm/glm-5.3 |
| DeepSeek V4.1 Flash (OpenRouter) | `reasoning:{effort}` (also `reasoning.max_tokens`, `exclude`); pass back `reasoning` or `reasoning_details` unmodified | `none`…`minimal`,`low`,`medium`,`high`,`xhigh`,`max` | 1,048,576; max completion 384,000; $0.15/$0.60 per M | https://openrouter.ai/docs/use-cases/reasoning-tokens ; `GET https://openrouter.ai/api/v1/models` (queried 2026-09-17) |

Relay's presets send `high` for Kimi and GLM (`R:backend/relay_core/presets.py`), below both
providers' `max` default. `ProviderConfig` caps `max_tokens` at 32768 (`R:provider.py`). All three
windows are 1M, so compaction is driven by **cost and latency** more than by overflow.

## Prioritized features

| # | P | Feature | Relay UX proposal | Size |
|---|---|---|---|---|
| 1 | P0 | **Model/effort switch keeps the conversation** | New worker message `set_model{preset, extra}` swaps `Agent.provider` between turns (queued if busy, like Claude Code's `/model` applying to the next request) instead of `configure`. Reasoning fields from another provider are stripped from history on switch, since Kimi and OpenRouter require their own unchanged. | S |
| 2 | P0 | **Slash menu in the composer** | `/` at column 0 with the destination Agent or Auto opens a filtered popup over the composer, reusing the palette engine (`docs/PALETTE-RESEARCH.md`). Commands: `/new` (`/clear`), `/model`, `/effort`, `/compact [focus]`, `/rewind`, `/fork`, `/plan [prompt]`, `/agents`, `/skills`, `/context`, `/export`, `/resume`, plus `/<skill>` loads a skill into the prompt. `/model /effort /agents /context` run immediately even while busy; the others follow Claude Code's queue rule. `/shell` and `/agent` prefixes keep working. | M |
| 3 | P0 | **Token accounting + context meter + compaction** | See design A. Meter in the input row next to the model picker: `ctx 142k · 14%`, amber above the soft limit. | M |
| 4 | P0 | **Checkpoints + `/rewind`** | See design B. | M |
| 5 | P0 | **Plan mode on Shift+Tab** | See the mode scheme below. Plan uses read-only tools, a `propose_plan` tool, and an approval card: *Execute* / *Execute in fresh context* / *Keep planning*. | S–M |
| 6 | P1 | **Subagents** (Claude Code model) | See design C. | L |
| 7 | P1 | **Persisted sessions, `/resume`, history list** | JSONL per conversation in `$XDG_STATE_HOME/relay/sessions/<workspace-hash>/<id>.jsonl` (0600). `/resume` opens a palette-style picker: title, age, model, message count, Space previews. Pane restore offers "resume last conversation". | M |
| 8 | P1 | **Fork** | `/fork` opens a split to the right with a copy of the history and the same model/effort (Warp's pane fork suits Relay's panes). The rewind picker gets **Fork from here** (opencode/Warp "fork from message"). `/fork-compact` forks with a summary. | S (after 7) |
| 9 | P1 | **Todo tool** | `update_todos{items[{text,status}]}` (one in_progress), emitted as a `todos` event and shown as a collapsible strip above the composer; printed inline only when it changes. | S |
| 10 | P1 | **Project instructions** | Load `AGENTS.md`, else `CLAUDE.md`, walking git root → workspace, 32 KiB cap, lower priority than Relay rules (OPENCODE-NOTES P5). Also `~/.config/relay/AGENTS.md`. Shown in `/context`. | S |
| 11 | P1 | **Esc stops; steer at step boundaries** | Esc in the composer while this pane's agent is busy stops it; otherwise it keeps "focus terminal". Enter while busy queues (as today); **Alt+Enter steers**: the prompt is appended before the next model call without cancelling (opencode/Codex). | S–M |
| 12 | P2 | Export (`/export` Markdown, copy last reply) | Reuse the inline transcript text. | S |
| 13 | P2 | Worktree isolation for writing subagents | `isolation: worktree` creates `git worktree add` under `$XDG_STATE_HOME/relay/worktrees`, removed if unchanged. | M |
| 14 | P2 | Whole-tree snapshots (shell side effects) | opencode-style separate `--git-dir` snapshot per turn, opt-in. | M |
| 15 | P2 | Optional approval modes | Add "Ask before writes/commands" to the Shift+Tab cycle only when the owner enables approvals again. | M |
| 16 | P2 | Hooks / custom commands | Skills already cover commands; hooks are deferred. | M |

## Mode scheme: Ctrl+I and Shift+Tab as two independent axes

- **Ctrl+I, destination** (unchanged): Auto → Terminal → Agent. It answers "where does Enter go?"
- **Shift+Tab, agent mode** (new, `agent.cycleMode`): **Build** (default: all tools) → **Plan** (read-only)
  → Build. It answers "what may the agent do?" The input-row pill reads e.g. `Auto · Plan · Kimi K3 · high`.
  Shift+Tab is free in the composer (only Alt/Ctrl+Shift+Tab are bound, `R:src/main.cpp:256-259`).
  It never reaches the shell because the composer has no completion. In native terminal input it stays Shift+Tab.
- Plan mode applies to every agent submission, including Terminal-mode auto-fix. In Plan mode the
  auto-fix loop is disabled, since it would need to run commands. Commands routed to the terminal still run,
  because the user typed them.
- Plan enforcement is a **tool filter**, not a prompt: `read_file`, `list_directory`, `load_skill`,
  `read_skill_file`, `update_todos`, `propose_plan`, and subagents whose tools are read-only. `run_command` is
  withheld in Plan, because unlike opencode (bash still allowed) Relay has no approvals to fall back on.
  A later read-only `search_files`/`find_files` (OPENCODE-NOTES P7) closes the exploration gap.
- `propose_plan{title, plan_markdown}` ends the turn and prints the plan inline with a card in the
  composer area: **Enter** Execute (switch to Build, send "Execute the approved plan"), **Ctrl+Enter** Execute
  in fresh context (new conversation seeded with the plan, as in Codex and Claude Code), **Esc** Keep planning.
  Saved to `.relay/plans/<ts>-<slug>.md` only if `.relay/` exists, otherwise to the state dir.

## Effort mapping (`/effort`, Alt+. / Alt+, like Codex)

Relay exposes one scale, `low · medium · high · max`, stored per pane and per preset:

| Relay level | Kimi K3 `reasoning_effort` | GLM-5.3 `thinking` + `reasoning_effort` | OpenRouter `reasoning.effort` |
|---|---|---|---|
| low | `low` | `{type:enabled}` + `low` | `low` |
| medium | `high` (no medium) | `high` (no medium) | `medium` |
| high | `high` | `high` | `high` |
| max | `max` | `max` | `max` (`xhigh` via custom extra) |

The picker shows only distinct levels per preset (Kimi/GLM: low, high, max). The effort is written into
`extra` at request time, so a custom `extra` still wins. Warn once that max costs the most.
Subagent definitions may set `effort`.

## Design A: token accounting and compaction

1. **Counting.** Request `stream_options:{include_usage:true}` (Kimi, GLM) and `usage:{include:true}`
   (OpenRouter). After each model call, store `prompt_tokens`, `completion_tokens`, cached tokens if
   reported, and reasoning tokens. Emit `{"event":"context","used":N,"limit":L,"soft_limit":S,"session_in":…,"session_out":…}`.
   If usage is missing, estimate as bytes/3.5 of the JSON payload, flagged `"estimated":true`.
2. **Limits.** Per-preset `context_limit` (1,000,000 for all three today) plus a user **soft limit**,
   default 200k, because a 1M prompt is slow and expensive on every step.
3. **Prune first** (above 70% of the soft limit): tool results older than the last 2 user turns become
   `{"elided":true,"bytes":N,"tool":…}` (opencode prune). This is free and keeps tool-call groups intact.
4. **Compact** (manual `/compact [focus]`, or automatic above 90% of the soft limit, checked between turns and
   before a model call): one tool-less call with a fixed template (Objective · Decisions · Files touched ·
   Commands run and results · Open items · Next step), preserving exact paths and identifiers (opencode and Codex
   templates). History becomes system + `[Relay summary of earlier conversation: model-generated, untrusted]`
   + the last 2 turns verbatim. Emit `compacted{before,after}` and print one dim inline line.
5. The rewind picker keeps pre-compaction checkpoints (design B), so compaction can be undone.

## Design B: checkpoints and `/rewind`

- **Checkpoint = user turn.** At turn start record `{turn_id, message_index, prompt, ts}`.
  `write_file` (and a future `edit_file`) already reads the old bytes and SHA before writing
  (`R:tools.py` `prepare`). Before the write, the executor stores the **pre-image** once per
  (turn, path) in `$XDG_STATE_HOME/relay/checkpoints/<conv>/<turn>/` (0700, content-addressed,
  128 KiB file cap already applies), plus the **post-write SHA**. A file created by the agent records "absent".
- **`/rewind`** (also a palette action, bound to Ctrl+Alt+Z. Esc Esc is not used, because Esc already focuses the
  terminal) opens a picker of past prompts, newest first, showing files changed per turn. Actions:
  **Restore conversation and files** · **Restore conversation** · **Restore files** · **Fork from here** ·
  **Summarize from here** · Cancel (Claude Code's menu plus Warp/opencode fork).
- **Restore files** walks turns newest → selected. For each path: if the current SHA equals the recorded
  post-write SHA, restore the pre-image (or delete if absent). Otherwise list it as a **conflict** (edited since by
  the user, the shell, or a subagent) and skip unless the user confirms. The original conversation is kept as a fork
  (Warp keeps a copy), so rewind is itself undoable.
- **Restore conversation** truncates `messages` to `message_index`, appends a Relay note ("conversation
  rewound; files may differ, reinspect"), and puts the prompt back in the composer.
- Say plainly: `run_command` side effects are **not** restored (all four tools say the same). Subagent writes
  are recorded with their agent id, so unlike Claude Code they *are* restorable.
- Worker: `{"type":"checkpoints"}` → `{"event":"checkpoints","items":[{turn,prompt_preview,files:[…]}]}`;
  `{"type":"rewind","turn":T,"conversation":true,"files":true,"force_paths":[]}` →
  `{"event":"rewound","restored":[…],"conflicts":[…]}`. Refused while any agent in the pane is busy.

## Design C: subagents

**Definitions.** Read Markdown files with YAML frontmatter from, in precedence order,
`<workspace>/.relay/agents/`, `<workspace>/.claude/agents/`, `~/.config/relay/agents/`, `~/.claude/agents/`,
so existing Claude Code agents work unchanged. Reuse `skills.parse_frontmatter`; the body is the system prompt.
Supported fields: `name`, `description` (required), `tools`, `disallowedTools`, `model`, `effort`,
`maxTurns`, `background`, `skills` (preload via `load_skill`), `color`, `isolation` (P2).
Unknown fields are ignored and reported in `configured`. Tool names map from Claude Code's
(`Read`→`read_file`, `Glob`/`LS`→`list_directory`, `Grep`/`Bash`→`run_command`, `Write`/`Edit`→`write_file`,
`Skill`→`load_skill`). `model` maps `inherit` → pane model, a Relay preset id → that preset, and
`haiku`/`sonnet`/`opus` → a user-editable alias table (default haiku→`openrouter`, others→inherit).
Built-ins: **explore** (read-only file tools and `run_command`, fast model, `effort: low`) and **general** (all
tools, inherit). Skills stay skills: an agent is a context-isolated worker, a skill is instructions.

**Tools the main agent gets:**
```json
{"name":"agent","parameters":{"description":"3-5 words","prompt":"full self-contained task",
 "subagent_type":"explore|general|<defined>","background":false,"model":"optional","effort":"optional"}}
{"name":"agent_message","parameters":{"agent_id":"a3","message":"follow-up or new instructions"}}
{"name":"agent_wait","parameters":{"agent_ids":["a3"],"timeout_seconds":120}}
```
Foreground `agent` blocks the parent's tool call and returns
`{"agent_id","status":"completed|error|stopped","result":"<final text>","tool_calls","duration_s","tokens"}`.
Several `agent` calls in one assistant message run **concurrently** (Relay allows 16 calls per response).
Background returns `{"agent_id","status":"running"}` at once. The final text is delivered later (see handoff).
Subagents never get `agent`/`agent_message` (depth 1, as in opencode and Codex V1), `set_keybinding`, or `update_todos`.

**Execution.** One thread per subagent inside the pane's existing worker process, each with its own
`Agent` (messages, `ChatProvider`, `ToolExecutor`, cancel event), under a new `SubagentSupervisor` beside
`TurnSupervisor`. This is I/O-bound work, so the GIL is fine. It inherits the pane's systemd scope and memory limit,
and **Restart agent** kills all of it. Limits: 4 concurrent per pane (configurable, max 8), `maxTurns` default 12,
same 24-tool budget per turn. **File safety:** the executor holds a per-path lease across agents in the pane,
so a write whose `old_sha` no longer matches fails, as it already does. That turns concurrent clobbering into a
visible error, and worktrees come in P2. Stopping the main turn stops foreground children. Background children
survive main-turn Stop and are stopped by `x`, "Stop all agents" (Ctrl+Shift+X), New chat, or pane close.
A later P2 option runs one worker process per subagent in its own scope, if memory isolation is needed.

**Result handoff and context isolation.** A child starts with its definition prompt, Relay's safety
rules, instructions (P1 #10), and the `prompt` only. It has no parent history. Only the final assistant text returns,
wrapped `[Result from subagent a3 (explore): untrusted model output]`. Background completions go into a
`pending_results` list that the parent drains **before its next model call** (between tool groups, never
inside one). If the parent is idle, Relay enqueues a synthetic `when:"queue"` turn, "Subagent a3 finished", so
the existing pause-on-error rule still applies (`docs/QUEUE-INTERRUPT.md`).

**UI: running-agents list under the composer** (Claude Code's panel adapted to Relay):
```
 ┌ composer ──────────────────────────────────────────────┐
 │ refactor the parser using the explorer findings         │
 └─────────────────────────────────────────────────────────┘
  Auto · Build · Kimi K3 · high · ctx 38k                     
  ● main        running · step 3/12                          
  ● explore a2  Find all callers of route()  0:41 · 9 tools · 22k tok   
  ✓ general a3  Draft migration notes        done 1:12 · 31k tok        
```
- Shown only when at least one subagent exists; at most 5 rows, then "+N more (/agents)".
- **Down** on the composer's last line when history is already at the draft (today a no-op) focuses the list.
  **Up/Down** move, **Up** past the first row returns to the composer, **Esc** returns too.
- **Enter** opens the agent's transcript in a `ToolPane` of a new `Kind::Agent` (split right, reusing the
  Explorer/Preview pattern) showing streamed text, tool calls, outputs, and diffs. The pane has its own small
  composer: Enter sends `agent_message` (resumes a finished agent with its history), Esc/Ctrl+W closes.
  When the window is narrow, a right-edge overlay like the actions palette is used instead.
- **x** stops a running agent or dismisses a finished row. **m** jumps to the message box. **Ctrl+Shift+X** stops all.
- Subagent tool activity is **not** printed into the terminal (it would bury the main transcript). The terminal
  gets one dim line at start (`↳ agent explore a2: Find all callers…`) and one at finish (`✓ a2 done 0:41 ·
  22k tok, result passed to main agent`). Background completion while Relay is unfocused uses the existing
  taskbar flash + `notify-send`. Finished rows stay 60 s, then move to `/agents`.
- `/agents` (palette submenu) lists definitions with source path and running/finished agents; Enter opens the transcript.
  Pane header cost: `main 38k · agents 53k tok` (dollar estimate only for OpenRouter, which publishes prices).

**Worker protocol additions** (existing events gain an optional `"agent"` field; absent = main):
```json
→ {"type":"agents_list"}
← {"event":"agent_definitions","items":[{"name":"explore","source":"builtin","tools":["read_file",…],"model":"inherit"}],"skipped":[…]}
← {"event":"subagent_started","agent":"a2","parent_turn":"<item id>","type":"explore","description":"Find all callers",
   "background":true,"model":"deepseek/deepseek-v4.1-flash","effort":"low"}
← {"event":"subagent_progress","agent":"a2","step":3,"tool":"run_command","preview":"rg -n 'route\\(' src"}
← {"event":"usage","agent":"a2","usage":{"prompt_tokens":21874,"completion_tokens":612}}
← {"event":"subagent_finished","agent":"a2","outcome":"completed|error|stopped","duration_ms":41200,
   "tool_calls":9,"tokens":{"in":21874,"out":1840},"result_preview":"first 200 chars"}
→ {"type":"agent_subscribe","agent":"a2"}      ← replays transcript, then streams delta/tool_* with "agent":"a2"
→ {"type":"agent_unsubscribe","agent":"a2"}
→ {"type":"agent_message","agent":"a2","text":"also check tests/"}   ← queued if running, resumes if finished
→ {"type":"agent_stop","agent":"a2"}   → {"type":"agent_stop_all"}
```
Only `subagent_*` summaries stream by default. Full deltas stream only while a transcript pane is subscribed,
which keeps the GUI pipe quiet with 4 agents running.

**Effort for design C:** definitions loader + mapping (S), `agent` tool + supervisor + concurrency + handoff (M),
list widget + keyboard focus rules (M), transcript `ToolPane` + subscribe protocol (M), tests (M) → **L** overall.
Ship it in two steps: (1) foreground-only `agent` with built-in `explore` and the inline start/finish lines;
(2) background, list, transcript pane, messaging.

## Open decisions for the owner

1. Should Plan mode withhold `run_command` entirely (proposed), or allow it with a read-only prompt, like opencode?
2. Should the Claude Code agent directories (`~/.claude/agents`, `.claude/agents`) load by default, or behind a toggle, since
   those prompts were written for Claude models and tools?
3. Default soft context limit (200k proposed), and whether auto-compact is on by default.
4. When a background subagent finishes and the main agent is idle, should Relay auto-start a main turn (proposed:
   yes, through the queue), or only notify?
