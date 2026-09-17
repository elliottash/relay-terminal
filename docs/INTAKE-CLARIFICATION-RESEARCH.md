# Intake clarification research: recaps, suggestions, @ files, compaction, handoff, instructions, agents, plans, busy detection

Research date: 2026-09-17. Answers the open questions in `issues/feature_intake.txt`. Nothing here is implemented.
Citation keys follow `docs/AGENT-FEATURES-RESEARCH.md`: **CC:x** = `https://code.claude.com/docs/en/x` (CLI 2.1.274 installed;
**CCL vN** = CC:changelog entry for version N), **W:x** = `https://docs.warp.dev/x`,
**OC:x** = `github.com/sst/opencode@88c6c7abc7f3`, **CX:x** = `github.com/openai/codex@e269f2164cbb` under `codex-rs/`,
**CXD:x** = `https://learn.chatgpt.com/docs/x`, **R:x** = this repo. **LAB** = measured on this machine (aarch64, kernel 7.0, sudo 1.9.15p5)
with a PTY probe script. *Unverified* marks anything not seen in docs, source, or LAB.

## 1. "Recaps"

| Tool | Name and trigger | Content and limits | Controls |
|---|---|---|---|
| Claude Code | **Session recap** ("away summary"). Generated in the background once ≥3 min have passed since the last completed turn **and** the terminal is unfocused. Needs ≥3 turns, never twice in a row, not while unsent text is in the prompt (CCL 2.1.108 added it) (CC:interactive-mode#session-recap) | One dim line (italic) above the prompt, ready when you switch back. Capped at 400 chars, cut at a word boundary (CCL 2.1.236) | `/recap` on demand; `/config` → Session recap; `awaySummaryEnabled`; `CLAUDE_CODE_ENABLE_AWAY_SUMMARY=0/1` (CC:env-vars). Skipped in `-p` mode |
| Claude Code | **Agent view attach recap**: on attaching to a background session, "Claude posts a short recap of what happened while you were away"; for background sessions the agent's own end-of-turn summary is used as the recap line (CC:agent-view; CCL 2.1.186) | One line | — |
| Claude Code | **Compaction summary** is *not* shown: the user sees "Conversation compacted", then one line per re-read file and "Skills restored". Summary keeps requests/intent, key concepts, files and snippets, errors and fixes, pending tasks, current work (CC:context-window) | Hidden | `/compact [focus]`; "Resume from summary" on large idle resumes (CC:sessions) |
| Claude Code | Footer `N done` pulse when a background agent finishes (CCL 2.1.212); `Notification` hook `agent_completed` in `claude agents` | No text summary | hooks |
| Codex CLI | **Conversation recap** (`/recap` "summarize the current conversation now"; `[tui] auto_recap`, default true). Auto when unfocused: deadline = max(unfocused_since, last_turn_end) + **30 min**; ≥3 completed turns; ≥2 turns since last recap (CX:tui/src/app/recap.rs:32-37,542-560; config/src/types.rs:776) | Cell headed "Conversation recap". Structured JSON `{summary ≤700 chars, next_action ≤200 or null}`; prompt: goal, completed progress, blockers, unfinished validation caveats, 40–60 words, from user-visible history only, ≤8,192 tok (CX:context-fragments/src/recap_prompt.rs) | `/recap` works with auto off |
| Warp | No recap feature found (no "recap"/"away" in WS app source). Nearest: collapsed **"Conversation summary" block** after `/compact` or automatic summarization when the window is exceeded (W:agents/cli/agent-conversations; W:agents/local-agents/interacting-with-agents); agent notifications/toasts (W:agents/capabilities/agent-notifications) | Summary block is expandable | `/compact <instructions>`, `/fork-and-compact` |

**Proposal for Relay ("Claude style recap"):** per pane, when the window is unfocused for ≥3 min after a finished turn and the
conversation has ≥3 turns, make one cheap tool-less call and print a dim `※ recap:` line above the composer; `/recap` on demand;
Codex's `{summary, next_action}` schema is the better prompt (it keeps "not yet tested" caveats). Setting: Recap when away (on).

## 2. Command suggestions

| Feature | Source and ranking | Accept / dismiss | Disable |
|---|---|---|---|
| **Warp autosuggestions** (ghost text, shell input) | (1) SQLite rich history: the ≤25 past runs of the *last command* with same pwd, exit code, host; count which command followed; take the 5 most common with the typed prefix; (2) else most recent history command with the prefix, same pwd first; (3) else first completion-spec result. Every candidate must pass `is_command_valid` (spec parse + file-path args exist) and not be in the ignored list | `→` or `Ctrl+F` full; `Ctrl+E` at end (macOS); `Ctrl+Shift+→` one component (Linux). Tab can be rebound to accept ("Tab key behavior"), moving completions to `Ctrl+Space` (W:terminal/command-completions/autosuggestions) | Command palette "Autosuggestions" toggle |
| **Warp Next Command** (AI) | LLM over history enriched with git branch, exit code, dir, plus recent block input/output; appears as ghost text on an empty/partial input after a command finishes; unlimited on all plans (W:agents/local-agents/active-ai) | `→`/`Ctrl+F`, rebindable inline | Settings › Agents › Warp Agent › Active AI › Next Command |
| **Warp Prompt Suggestions** | LLM on the most recent block; chip that switches to Agent mode | `Alt+Shift+Enter` (Linux) or click | Active AI › Prompt Suggestions |
| **Warp Command Corrections** | thefuck-based rules (git, cd, sudo, npm, pip, generic misspelling…) after a failed command; panel above input (W:terminal/entry/command-corrections) | click or `→` | Features › Terminal Input › "Suggest corrected commands" |
| **Warp completions** | Fuzzy completion specs, aliases expanded; `Tab` menu, optional "open as you type" (W:terminal/command-completions/completions) | ↑/↓, Enter | — |
| **Claude Code prompt suggestions** | (a) session start: grey example prompt picked from the project's git history; (b) after each response: predicted next prompt via a background request reusing the prompt cache. Skipped when cache is cold, after errors, in plan mode, near usage limit; **off by default on third-party providers** (CC:interactive-mode#prompt-suggestions) | `Tab` or `→` places it; Enter then submits (Enter on empty input no longer auto-submits, CCL 2.1.136); typing dismisses | `/config` Prompt suggestions; `promptSuggestionEnabled:false`; `CLAUDE_CODE_ENABLE_PROMPT_SUGGESTION=false` |
| Claude Code mid-prompt `/command` ghost text | top match as ghost text with `+N` count (non-fullscreen) (CC:interactive-mode) | `Tab` | — |
| **fish** | history, completions, valid paths (https://fishshell.com/docs/current/interactive.html) | `→`/`Ctrl+F` full, `Alt+→`/`Alt+F` one word | `set -g fish_autosuggestion_enabled 0` |
| Codex CLI | No next-prompt or command ghost text found in `tui/src` (*unverified absence*) | — | — |

**Relay fit:** in Terminal/Auto destination, ghost text from Relay's own per-workspace history (Warp step 2: prefix match, same cwd
first, then global recency), accepted with `→`/`Ctrl+F` at end of line, `Alt+→` one word; never Tab (Tab stays completion/indent).
After an agent turn, optional LLM next-prompt ghost (Claude Code) — off by default because BYOK calls cost the user money.

## 3. Open files by name / `@`

| Tool | Trigger and scope | Index and ranking | What gets attached |
|---|---|---|---|
| Claude Code | `@` in prompt opens path menu; Enter/Tab accepts (CC:common-workflows#reference-files-and-directories) | Index from `git ls-files`, falling back to ripgrep (strings in CLI 2.1.274 bundle); gitignored files hidden by default (`respectGitignore`, default true); replaceable by `fileSuggestion` command receiving `{"query"}` JSON, 5 s timeout (CC:settings-reference). Scoring *unverified* | File: **full contents**; directory: listing only; also loads `CLAUDE.md` of that dir and parents; `@server:resource` for MCP |
| Warp `@` | Agent prompt and classic commands; search always from **git repo root**, no indexing needed, respects `.gitignore`; also symbols, Drive objects, blocks from other sessions, plans (W:agents/local-agents/agent-context/using-to-add-context) | Zero-state: git-changed and recently opened files first. Query: fuzzy per whitespace term (scores summed), wildcards, filename match ×2 + path, exact filename +5000, bonus for matches near the end of the path | Reference token; contents passed as context |
| Warp file open | `Ctrl+Shift+O` (Linux) file search in palette, `files:` filter; opens in Warp's code editor pane (W:code/code-editor; W:terminal/command-palette) | Same FileSearchModel | Opens editor pane |
| opencode | `@` files, agents, MCP resources (OC:tui/component/prompt/autocomplete.tsx) | `fff` backend ordered by **frecency + fuzzy**; fallback ripgrep file list + `fuzzysort`, limit 50 (OC:packages/core/src/filesystem/search.ts:103-115) | File part |
| Codex | `@` file search (CX:tui/src/keymap.rs) | `nucleo` fuzzy; `ignore` walker, hidden files included, follows symlinks, gitignore only inside git repos (CX:file-search/src/lib.rs:427-442) | Path inserted |
| VS Code Quick Open | `Ctrl+P`; repeat to cycle recent | Recently opened (history) first, then fuzzy file matches; excludes `files.exclude`, `search.exclude`, `.gitignore` (`search.useIgnoreFiles`); `search.quickOpen.includeHistory` (https://github.com/microsoft/vscode/wiki/Search-Issues; https://code.visualstudio.com/docs/getstarted/tips-and-tricks) | Opens editor |

**Relay fit (owner: "any file that can be previewed in a pane will then start showing up with a type text filter"):**
`@` at a word start in any composer mode opens one popup; zero-state = files opened in Preview recently + `git status` changes;
query = fuzzy (filename ×2, exact +large, end-of-path bonus) over `git ls-files -co --exclude-standard` (ripgrep `--files` outside git).
Enter inserts `@path`; `Ctrl+Enter` (or a palette "Open file…", `Ctrl+P` in VS Code preset) opens it in a Preview pane instead.
In Agent mode an `@file` sends contents (≤128 KiB cap as in `R:tools.py`), directories send a listing.

## 4. Compaction thresholds

| Tool | Trigger | Knobs / defaults |
|---|---|---|
| Claude Code | Default: compact when the conversation reaches the model's context limit; native-1M models compact "at about 967K" (≈92% of 1,048,576); 200K models at the 200K boundary; unknown model IDs at the assumed window (CC:model-config#default-auto-compact-thresholds; CCL 2.1.247, 2.1.260) | `/autocompact 500k`, `--autocompact`, `autoCompactWindow` (100K–1M), `CLAUDE_CODE_AUTO_COMPACT_WINDOW` (plain int, wins), `CLAUDE_AUTOCOMPACT_PCT_OVERRIDE` (1–100, can only lower), `DISABLE_AUTO_COMPACT`, `DISABLE_COMPACT`, `CLAUDE_CODE_MAX_CONTEXT_TOKENS` (CC:env-vars). Circuit breaker after 3 failures; thrash detection |
| Codex | `auto_compact_token_limit = min(model_auto_compact_token_limit, 90% × context_window)`; inference sees `effective_context_window_percent` = **95%** of the window; checked pre-turn and mid-turn (CX:protocol/src/openai_models.rs:389,515-532; core/src/session/turn.rs) | `model_auto_compact_token_limit`, `model_context_window`, `model_auto_compact_token_limit_scope = total\|body_after_prefix` (CX:config/src/config_toml.rs:165-173) |
| opencode | `count ≥ usable`; usable = `limit.input − min(20_000, maxOutput)` if the model declares an input limit, else `context − maxOutput`; `maxOutput = min(model.limit.output, 32_000)` (OC:packages/opencode/src/session/overflow.ts; provider/transform.ts:18,1468) | `compaction.auto:false`, `compaction.reserved` |
| Warp | "If the context window is exceeded … Warp will automatically summarize"; meter hidden <20%, red near limit (W:agents/local-agents/interacting-with-agents). Exact threshold is server-side (*unverified*) | `/compact`, `/fork-and-compact` |

**Model windows (verified 2026-09-17):**

| Relay preset | Provider doc | OpenRouter `/models` | Endpoint caveat (`/models/<id>/endpoints`) |
|---|---|---|---|
| Kimi K3 | 1M; `max_completion_tokens` default 131,072, up to 1,048,576 (https://platform.kimi.ai/docs/guide/kimi-k3-quickstart) | `moonshotai/kimi-k3` 1,048,576 | all 20 endpoints 1,048,576; max output varies (DeepInfra 16,384) |
| GLM-5.3 | 1M, output 128K (https://docs.z.ai/guides/llm/glm-5.3) | `z-ai/glm-5.3` lists 1,310,720; top provider 1,048,575 | **Reka 262,144, Io Net 262,124, Alibaba/Venice 1,000,000, Makora 980,000** |
| DeepSeek V4.1 Flash | 1M, output max 384K (https://api-docs.deepseek.com/quick_start/pricing) | `deepseek/deepseek-v4.1-flash` 1,048,576, max completion 384,000 | Alibaba/Venice 1,000,000 |

**Recommendation.** Auto-compact when `used ≥ min(0.80 × W, W − R − 24K)`, where **W** = the smallest context among the endpoints
the request can route to (pin `provider.order`/`only` for OpenRouter, otherwise use the minimum: GLM-5.3 unpinned can land on a
262K endpoint), **R** = the `max_tokens` Relay requests (32,768 today, `R:provider.py`), 24K = summary call output (~8–16K) plus
margin for the next tool result. For all three presets pinned at 1,048,576 that is **838,860 tokens (80%)**. Reasons: (a) the
summary call itself needs the full history plus room for its output, so compacting at 95–100% (Claude Code, opencode) leaves no
retry room when one tool result is large; Codex's 90% is the aggressive end; (b) long-context quality falls well before the limit
on retrieval-style tasks (Chroma "Context Rot", https://research.trychroma.com/context-rot; NoLiMa, https://arxiv.org/abs/2502.05167),
and open-weight models are less tuned for it than Anthropic/OpenAI ones (*unverified for these three models*); (c) cost/latency
per step grows linearly with context, which the separate user soft limit (200K proposed in AGENT-FEATURES-RESEARCH Design A)
handles. Expose it as `Compact at: 80%` with a token readout, plus `RELAY_AUTOCOMPACT_PCT` for scripts.

## 5. Background subagent finishes while the main agent is idle

| Tool | Behavior | Evidence |
|---|---|---|
| Claude Code | **Starts a new main turn automatically.** The result is a `<task-notification>` delivered to the model; between turns it is sent inside `<system-reminder>` tags (CCL 2.1.234) and marked as containing no human input (CCL 2.1.205). Docs: "results reach Claude as a completion notification in a later turn"; forks' results "arrive as a message in your main conversation" (CC:sub-agents). Model-facing tool text: "you will be automatically notified when it completes — do NOT sleep, poll" (CLI bundle strings). Observed in this very session: idle parent resumes on completion | Docs do not literally say "idle session wakes"; the changelog "pending … background-task notification re-sent as the prompt" (CCL 2.1.243) and post-response timer "Waiting for N background agents" (CCL 2.1.152) corroborate |
| Warp | **Starts a new request automatically.** Child messages/lifecycle events are queued in `OrchestrationEventService`; `EventsReady` → if the parent conversation status is Success or WaitingForEvents with no active stream, pending events are injected and a resume request is sent, cancelling any `wait_for_events` (2047-2075,2124-2141) | Source |
| opencode | **Starts a new loop**: background `task` completion calls `ops.prompt` with a synthetic text part "Background task completed: …" (OC:packages/opencode/src/tool/task.ts:227-265) | Source |
| Codex V1 | Injects `<subagent_notification>` **without** starting a turn (see AGENT-FEATURES-RESEARCH §4) | Source |

Recommendation stands (AGENT-FEATURES-RESEARCH open decision 4): auto-start through the queue, with a per-pane cap (e.g. 3 automatic
wakes without user input, like Claude Code `/goal` check-ins, CCL 2.1.246) so a chain of children cannot spend the key unattended.

## 6. Instruction / memory files to scan on first run

| Tool | Global | Project | Format | Precedence / loading |
|---|---|---|---|---|
| Claude Code | `~/.claude/CLAUDE.md`, `~/.claude/rules/**/*.md`; managed `/etc/claude-code/CLAUDE.md` | `CLAUDE.md` or `.claude/CLAUDE.md` in cwd and every parent; `CLAUDE.local.md`; `.claude/rules/**/*.md` (`paths:` frontmatter = conditional); subdir CLAUDE.md on demand | Markdown, `@import` (4 hops) | All concatenated root→cwd, nearer read last. **Does not read AGENTS.md** (recommends `@AGENTS.md` import) (CC:memory) |
| Codex | `~/.codex/AGENTS.override.md` or `AGENTS.md` | per dir root→cwd: `AGENTS.override.md` > `AGENTS.md` > `project_doc_fallback_filenames` | Markdown | Concatenated, 32 KiB cap, untrusted projects skipped (CX:core/src/agents_md.rs) |
| Warp | Global Rules in Warp Drive (cloud, no file) | `AGENTS.md` or `WARP.md` (all caps; **WARP.md wins** in same dir) at repo root + cwd; subdirs best effort | Markdown | subdir > root > global. `/init` can link `CLAUDE.md`, `.cursorrules`, `AGENT.md`, `GEMINI.md`, `.clinerules`, `.windsurfrules`, `.github/copilot-instructions.md` (W:agents/capabilities/rules) |
| opencode | `~/.config/opencode/AGENTS.md`, else `~/.claude/CLAUDE.md` | first of `AGENTS.md`/`CLAUDE.md` walking up; `instructions` globs/URLs in `opencode.json` | Markdown | first match per category (https://opencode.ai/docs/rules/) |
| Gemini CLI | `~/.gemini/GEMINI.md` | `GEMINI.md` in workspace dirs and ancestors; JIT in dirs tools touch | Markdown, `@file.md` | all concatenated; names via `context.fileName` e.g. `["AGENTS.md","GEMINI.md"]` (https://geminicli.com/docs/cli/gemini-md/) |
| Cursor | User Rules in settings UI (no file); Team Rules dashboard | `.cursor/rules/**/*.mdc` (`description`, `globs`, `alwaysApply`); `AGENTS.md` root+nested; legacy `.cursorrules` | MDC / Markdown | Team → Project → User (https://cursor.com/docs/context/rules) |
| GitHub Copilot | personal instructions (location not in doc, *unverified*) | `.github/copilot-instructions.md`; `.github/instructions/*.instructions.md` (`applyTo`, `excludeAgent`); `AGENTS.md` (nearest), `CLAUDE.md`, `GEMINI.md` (root) | Markdown | personal > repository > organization; path + repo-wide both used (https://docs.github.com/en/copilot/how-tos/configure-custom-instructions/add-repository-instructions) |
| Windsurf / Devin Desktop | `~/.codeium/windsurf/memories/global_rules.md` (6K chars) | `.devin/rules/*.md` > `.windsurf/rules/*.md`; legacy `.windsurfrules`; `AGENTS.md` anywhere (root always-on, subdir = glob); 12K chars/file | `trigger: always_on\|model_decision\|glob\|manual` | searched up to git root (https://docs.devin.ai/desktop/cascade/memories) |
| Cline | `~/Documents/Cline/Rules` (Linux fallback `~/Cline/Rules`); `~/.agents/AGENTS.md` | `.clinerules` file or dir; also `.cursorrules`, `.windsurfrules`, `AGENTS.md` | Markdown, `paths:` frontmatter | combined; workspace wins on conflict (https://docs.cline.bot/features/cline-rules) |
| aider | none automatic | `CONVENTIONS.md` only via `--read`, `/read`, or `read:` in `.aider.conf.yml` | Markdown | explicit only (https://aider.chat/docs/usage/conventions.html) |
| Zed | `~/.config/zed/AGENTS.md` | **first match only**: `.rules`, `.cursorrules`, `.windsurfrules`, `.clinerules`, `.github/copilot-instructions.md`, `AGENT.md`, `AGENTS.md`, `CLAUDE.md`, `GEMINI.md` | Markdown | project overrides personal (https://zed.dev/docs/ai/instructions) |
| Continue | `~/.continue/rules` (*unverified*) | `.continue/rules/*.md` (`name`, `globs`, `regex`, `description`, `alwaysApply`), lexicographic | Markdown | (https://docs.continue.dev/customize/deep-dives/rules) |
| Junie | `~/.junie/AGENTS.md` | `.junie/AGENTS.md` (exclusive) > `AGENTS.md` + `.junie/playbook.md` + `.junie/rules/*.md` > legacy `.junie/guidelines.md` or `.junie/guidelines/` | Markdown | both included, project wins, deduplicated (https://junie.jetbrains.com/docs/guidelines-and-memory.html) |
| Kiro | `~/.kiro/steering/*.md` (+ `AGENTS.md` there) | `.kiro/steering/*.md`; `AGENTS.md` root and subdirs | `inclusion: always\|fileMatch (fileMatchPattern)\|manual\|auto` | workspace wins (https://kiro.dev/docs/steering/) |

**Relay scan order proposal** (first run shows what was found, user ticks which to load): project, from git root to workspace:
`AGENTS.md` > `CLAUDE.md` (+`CLAUDE.local.md`, `.claude/rules/*.md` without `paths:`) > `WARP.md` > `GEMINI.md` >
`.github/copilot-instructions.md` > `.cursor/rules/*.mdc` with `alwaysApply: true` > `.cursorrules` > `.windsurfrules`/`.windsurf/rules`
(`always_on`) > `.clinerules` > `.rules` > `.junie/guidelines.md` > `.kiro/steering` (`always`). Global: `~/.config/relay/AGENTS.md`,
`~/.codex/AGENTS.md`, `~/.claude/CLAUDE.md`, `~/.gemini/GEMINI.md`, `~/.config/opencode/AGENTS.md`, `~/.config/zed/AGENTS.md`.
Default: load the first project hit per directory plus one global file (avoids duplicate text when CLAUDE.md is `@AGENTS.md`); resolve
`@imports`; 32 KiB total cap; conditional rules (globs/paths/fileMatch) load only when the agent reads a matching file.

## 7. Agent definition files

| Tool | Locations (precedence) | Format and fields |
|---|---|---|
| Claude Code | managed > `--agents` JSON > `.claude/agents/` > `~/.claude/agents/` > plugins (CC:sub-agents) | Markdown + YAML; body = system prompt. `name`*, `description`*, `tools`, `disallowedTools`, `model` (`sonnet/opus/haiku/fable`/ID/`inherit`), `permissionMode`, `maxTurns`, `skills`, `mcpServers`, `hooks`, `memory` (`user/project/local`), `background`, `omitClaudeMd`, `effort`, `isolation: worktree`, `color`, `initialPrompt`, `experimental` |
| opencode | `.opencode/{agent,agents}/**/*.md`, `~/.config/opencode/{agent,agents}/**/*.md`, or `agent` in `opencode.json` (OC:oc/config/agent.ts:13) | Markdown + YAML; filename = name. `description`, `mode` (`primary/subagent/all`), `model`, `variant`, `temperature`, `top_p`, `steps`, `permission`, `hidden`, `color`, `prompt` |
| Codex | `~/.codex/agents/*.toml`, `.codex/agents/*.toml`; also `[agents.<role>]` in `config.toml` (CXD:agent-configuration/subagents) | TOML. `name`*, `description`*, `developer_instructions`*; any config key: `model`, `model_reasoning_effort`, `sandbox_mode`, `mcp_servers`, `skills.config`; roles `nickname_candidates`. Separate concept: `[profiles.<name>]` config profiles |
| Gemini CLI | `.gemini/agents/*.md`, `~/.gemini/agents/*.md` (https://geminicli.com/docs/core/subagents/) | Markdown + YAML. `name`*, `description`*, `kind` (`local/remote`), `tools` (wildcards), `mcpServers`, `model` (`inherit`), `temperature` (1), `max_turns` (30), `timeout_mins` (10) |
| Cursor | `.cursor/agents/` > `.claude/agents/` > `.codex/agents/`; same under `~/`; project beats user (https://cursor.com/docs/context/subagents) | Markdown + YAML. `name`, `description`, `model` (`inherit`), `readonly`, `is_background` |
| Warp | **No agent files.** Agent Profiles in Settings (name, base model, planning model, per-action autonomy, allow/deny lists, MCP access) (W:agents/capabilities/agent-profiles-permissions); "skills as agents" for `oz agent run` from `.warp/skills`, `.agents/skills`, `.claude/skills`, `.codex/skills`, `.cursor/skills`, `.gemini/skills`, … (W:platform/skills-as-agents) | Profile UI; skills = `SKILL.md` |

**Common model for Relay** (`AgentDef`): `name` (frontmatter or filename), `description`, `prompt` (Markdown body, or Codex
`developer_instructions`), `tools` allow + `deny` (CC `tools/disallowedTools`; Gemini `tools`; Cursor `readonly` → read-only set;
opencode `permission` edit/bash deny → drop `write_file`/`run_command`), `model` (`inherit` default; CC aliases and foreign IDs via
the alias table), `effort` (CC `effort`, Codex `model_reasoning_effort`, opencode `variant`), `max_turns` (CC `maxTurns`, opencode
`steps`, Gemini `max_turns`), `background` (CC `background`, Cursor `is_background`), `temperature`, `timeout`, `color`, `hidden`
(opencode, or `mode: primary`), `source` path. Ignore and report: `hooks`, `mcpServers`, `memory`, `isolation`, `sandbox_mode`, `kind: remote`.
Scan order: `.relay/agents` > `.claude/agents` > `.opencode/agent(s)` > `.gemini/agents` > `.cursor/agents` > `.codex/agents` (toml) >
same under `~/.config/relay`, `~/.claude`, `~/.config/opencode`, `~/.gemini`, `~/.cursor`, `~/.codex`; first definition of a name wins.

## 8. Plan mode "like Warp"

| Aspect | Warp | Claude Code / opencode (contrast) |
|---|---|---|
| Entry | `/plan [task]` "Prompt the agent to do some research and create a plan"; sent as `UserQueryMode::Plan` to Warp's server, which applies plan behavior | Shift+Tab mode; CC plan files in `~/.claude/plans` (`plansDirectory`), Ctrl+G edits in `$EDITOR`; opencode `.opencode/plans/*.md` |
| Tools while planning | Not enforced client-side; the server-side plan prompt and the profile's permissions decide. Docs describe research then a plan; the profile has a separate **"Create plans"** permission and optional **planning model** (W:agents/capabilities/agent-profiles-permissions). Whether commands run during planning is server-defined (*unverified*); Warp's permission model would still gate them | CC: read-only, edits blocked; opencode: bash allowed, edits only to plan file |
| Artifact | Agent tool `CreateDocuments` → **AI document pane** with a rich text editor (formatting, code blocks, clickable paths); `EditDocuments` revises; `ReadDocuments` | Markdown file |
| Editing and versions | User edits directly; every agent update **creates a new version**, history lets you diff/restore (W:agents/capabilities/planning) | none |
| Storage | Local SQLite (`SaveAIDocumentContent`) and autosync every 2 s to Warp Drive *Plans* folder; export Markdown, share link | file |
| Execute | **No approve button**: the user prompts "implement the plan" or "phase 1", can `@plans` it later; "View plan" chip in the input; editing during a run → prompt to notify the agent (W:agents/capabilities/planning) | CC approval card; opencode `plan_exit` |

**Relay design matching the owner's wording:** `/plan <task>` (and Shift+Tab Plan) keeps `run_command` **allowed** for investigation
but the system note says "do not modify files; commands must be read-only", and `write_file` is limited to the plan path (opencode's
rule, since Relay has no approvals). The `propose_plan{title, markdown}` tool writes `.relay/plans/<ts>-<slug>.md` (state dir if no
`.relay/`) and opens it in a split **editable text pane** (the Preview pane in edit mode, saved on change, backed by git-less versions
`<file>.v1…` per agent revision). The composer shows a chip `Plan ready · Enter Execute · Ctrl+Enter Execute fresh · Esc Keep planning`;
Execute re-reads the file from disk so user edits win. This supersedes the "withhold run_command" proposal in AGENT-FEATURES-RESEARCH.

## 9. Detecting "waiting for input" and "full-screen" from outside the process

Relay already polls termios through `/proc/<shell>/fd/0` and reads `tpgid` from `/proc/<shell>/stat` because `tcgetpgrp()` on a
tty that is not the caller's controlling terminal fails with `ENOTTY` (R:src/main.cpp:1226-1245,1704-1716; confirmed in LAB).
Signals available without being the controlling process: (1) termios `ICANON`/`ECHO` of the pty (any fd on it, `tcgetattr`);
(2) foreground group = `tpgid`; (3) `/proc/<pid>/wchan` and `/proc/<pid>/syscall` for each process in that group (same uid only;
syscall nr is arch-specific: aarch64 read=63, ppoll=73, pselect6=72, epoll_pwait=22, wait4=260); arg0 of `read` + `/proc/<pid>/fd/N`
says whether the read is on the tty; (4) alternate screen (`CSI ?1049/1047/47 h`) — only the emulator knows. Konsole tracks it
(`Emulation::primaryScreenInUse(bool)` → `Session::primaryScreenInUse`, konsole `src/Emulation.h:399`, `src/ViewManager.cpp:898`)
but **KonsolePart's public API does not expose it** (`src/Part.h` signals: overrideShortcut, silence/activity, currentDirectoryChanged).
Getting it needs a Konsole patch, a private-object hack (*unverified*), or Relay parsing its own copy of the pty stream.

**Warp's rule** (source): the input editor hides when the active block has been executing ≥ **50 ms** (`LONG_RUNNING_COMMAND_DURATION_MS`,1706-1735), known from shell-integration hooks, not process inspection; alt-screen switches to
the full-screen grid (`is_alt_screen_active`); a password prompt is `!ECHO && ICANON`, polled every
**1 s** with `tcgetattr` on the pty while a block runs. Running commands get a "Use Agent" footer for Full Terminal Use.

**LAB results** (bash 5 `--norc -i` in a PTY, probe 2.5–4 s after starting the command):

| Program (state) | ICANON | ECHO | alt | Foreground process: wchan / syscall | Reliable classification |
|---|---|---|---|---|---|
| bash prompt (readline) | 0 | 0 | 0 | bash: pselect6 | idle shell = fg pgid == shell pid |
| `cat` / `head -n1` apt-style `[Y/n]` / `read -p` | 1 | 1 | 0 | `wait_woken` / `read` → `/dev/pts/N` | **waiting for line input: high** |
| password (`getpass`, like sudo) | 1 | **0** | 0 | `wait_woken` / `read` → `/dev/tty` | **password: high** (Warp rule) |
| python3 REPL | 0 | 0 | 0 | pselect6 | raw, not alt → "interactive line app", input likely |
| node readline question (npm init style) | 0 | 0 | 0 | `ep_poll` / epoll_pwait | raw, not alt; indistinguishable from busy node except termios |
| npm-like busy node | 1 | 1 | 0 | `ep_poll` / epoll_pwait | busy: high (canonical, not reading tty) |
| `make` → `sleep` | 1 | 1 | 0 | make `do_wait`/wait4; sleep nanosleep | busy: high |
| ssh host-key prompt (before connect) | 1 | 1 | 0 | `read` → `/dev/tty` | waiting for input: high |
| ssh connected (idle remote, remote `[Y/n]`, remote busy) | 0 | 0 | 0 | ppoll (all three identical) | **cannot tell**; remote vim → alt=1 only via emulator |
| less | 0 | 0 | **1** | `read` → pts | full-screen: needs alt |
| vim | 0 | 0 | **1** | pselect6 | full-screen: needs alt |
| htop | 0 | 0 | **1** | ppoll | full-screen: needs alt |
| `sudo head -n1` (apt-style prompt under sudo) | **0** | **0** | 0 | sudo: wchan/syscall **EACCES** (root) | **cannot tell** |

`sudo apt upgrade` specifically: sudo's own password prompt is on the user's tty before elevation (`ICANON && !ECHO`, detectable).
After that, sudo ≥1.9.14 runs the command in its own pty by default (`use_pty` default since 1.9.14, https://github.com/sudo-project/sudo/blob/main/NEWS ; observed LAB:
outer tty goes raw), so apt's download progress and its **"Do you want to continue? [Y/n]"** look identical from outside (raw, root-owned
`sudo` in poll). Apt's needrestart/debconf whiptail dialogs use the alternate screen (*unverified*); apt-listchanges pages via `less`.

**Recommended rule for the intake ("only hide the prompt box for full-screen apps and password input"):**
1. Hide/defocus composer only when **alt screen is active** (primary signal; requires exposing Konsole's `primaryScreenInUse`), or
   **password** (`ICANON && !ECHO`, as today). Without the alt-screen signal, fall back to: fg ≠ shell, `!ICANON`, and fg `comm` in a
   known full-screen list (vim, nvim, htop, top, less, man, tmux, screen, mc, nano, emacs -nw, lazygit, k9s…), accepting misses.
2. Everything else keeps the composer visible. Show a **"waiting for input" hint** only when the fg process is in `read` on the tty
   with `ICANON` set (cat, `[Y/n]` without sudo, ssh host-key) or when output has been quiet ≥1 s and the last screen line matches
   `\[[Yy]/[Nn]\]\s*$|\(yes/no[^)]*\)\??\s*$|[Pp]assword[^:]*:\s*$|Continue\?|\?\s*$`; the text heuristic is the only thing that
   catches sudo-wrapped apt and remote prompts over ssh. Enter from the composer while a program runs sends the line to the pty
   (with a visible "to program" destination) or queues it after the program exits, user's choice per submission.
3. Poll only while fg ≠ shell, every 250–500 ms (Warp appears to use about 1 s); `/proc` reads cost microseconds.
