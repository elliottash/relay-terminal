---
id: GT7X
type: work
status: in-progress
labels: [feature]
component: [worker, gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
rank: '59'
created: '2026-09-19'
acceptance: claude or codex running in a pane gets composer routing with guest slash autocomplete, pane status and notifications from hooks, guest sessions in the sessions pane with resume, and claude openDiff rendered in a Relay diff view
source: 'conversation, 2026-09-19: "research how warp has the plugins to nicely interact with claude and codex… plan a similar feature for relay terminal"'
links: {plans: [], commits: [], evidence: [], related: [SSRQ], github: null}
---
# Claude Code + Codex guest integration (translator first, harness second)

## Issue
"do research on how warp has the plugins to nicely interact with claude and codex. write detailed research on how that
works, including looking at the docs and github, of warp and the associated plugins / connectors. plan a similar
feature for relay terminal, so that we can have frictionless interaction with claude / codex. ideally we can not even
show the prompt boxes there, as the relay terminal prompt box becomes a superior rich text upgrade."

"i dont need a headless protocol. it could work to have claude / codex sitting in the terminal pane, and there is a
special translator from the relay prompt into the claude code input and back."

"great, update the plan as needed and execute"

## Tasks
- [ ] Foundation: guest registry, foreground detection of claude/codex, pane guest state
- [ ] Claude IDE bridge (WebSocket MCP `ide` server, `~/.claude/ide` lock file, 12 tools, openDiff → Relay diff view)
- [ ] Claude hooks + statusline shim (notifications, pane states, context/model chips)
- [ ] Codex Tier B: daemon co-attach spike → hooks (+ stream attach or rollout tail)
- [ ] Composer translator + slash registry (guest autocomplete, TUI-state-aware injection)
- [ ] Sessions pane: claude + codex conv_index sources, resume/fork, unified search
- [ ] Validation: scripts/test.sh + ctest + Xvfb live run; QA evidence
- [x] ~~Tier A headless harness adapters (codex app-server first, then claude stream-json)~~ — deferred by owner, kept as later optional phase

## Decisions
- 2026-09-19, owner: translator-in-pane first ("i dont need a headless protocol… a special translator from the relay
  prompt into the claude code input and back"); headless/app-server adapters deferred to a later optional phase.
- 2026-09-19, owner: keep the terminal/agent/auto-detect system; where Relay lacks a guest capability, Relay adopts it
  as / commands (guest badge in the slash popup, pass-through to the guest).
- 2026-09-19, owner: no screen scraping for state — structured sources only (IDE bridge, hooks, statusline JSON,
  transcript/rollout tailing, app-server daemon).
- 2026-09-19, agent: Relay implements the same protocols VS Code uses — the claude IDE bridge (lock file + WebSocket
  MCP) now, and claude stream-json + codex app-server as a client in the deferred Tier A phase.

## Functionality mapping
| Capability | Claude Code | Codex | Relay equivalent | Integration path |
|---|---|---|---|---|
| Prompt input | TUI prompt / stream-json user msg | TUI prompt / `turn/start` | Composer | Tier B: `type_into_program`; Tier A: protocol |
| Slash commands | built-ins + skills + `.claude/commands` | TUI commands | Relay `/` commands + aliases + skills | Union registry, guest badge, passthrough |
| `/resume` | TUI picker; `claude -r <id>` / `-c` | `/resume` picker; `codex resume` | Sessions pane | conv_index sources + respawn command |
| `/model` | `/model`, `--model`, `set_model` | `/model`, `thread/settings/update`, `model/list` | Model picker | Tier B: type command; Tier A: protocol |
| Context % | statusline JSON; `result.usage` | `turn/completed` usage; `token_count` | Context chip | Statusline shim / app-server events |
| Compact | `/compact` | `thread/compact/start` | `/compact` | Passthrough (B) / protocol (A) |
| Permissions | permission modes; PermissionRequest hook; `can_use_tool` | approval policies; `requestApproval`; `pre_tool_use` hook | Relay agent keeps no-approval model; guests get approval UI | openDiff bridge (edits), hook decisions, cards (A) |
| Edit review | `openDiff` (IDE bridge, blocking FILE_SAVED/DIFF_REJECTED) | `applyPatchApproval` | Relay diff view | Bridge (B) / adapter (A) |
| Sessions store | `~/.claude/projects/**/*.jsonl` | rollouts + `state_5.sqlite` threads | conv_index + sessions pane | Indexers, live tail for active pane |
| Fork | `--fork-session` | `thread/fork` | `/fork` | Respawn with flag (B) / protocol (A) |
| Rewind/checkpoints | `/rewind` (TUI-only) | `thread/rollback` | `/rewind`, `/rewind-code` | B: pass to TUI; A: rollback (codex), documented limitation (claude) |
| `!` shell | `!` bash mode | `thread/shellCommand` | Relay shell routing | Explicit user choice, never silent |
| `@` files | `@` mentions | `@` mentions | Relay `@` picker | Composer translates to guest syntax |
| `#` memory | `#` shortcut | — | Instructions system | Passthrough for claude guests |
| Notifications | hooks (Stop/Notification/PermissionRequest) | hooks + `[tui] notification_condition` | Notification centre | Hook bundles → pane events |
| Statusline | `statusLine.command` JSON (model, cwd, context %) | — | Prompt items / chips | Shim forwards JSON to pane |
| Subagents | Task tool | `spawn_agent` | Relay subagent pane | Event bridge (polish phase) |
| Todos | TodoWrite | — | Relay todos | Event bridge (polish phase) |
| MCP | MCP servers, `/mcp` | MCP servers | (SSRQ: planned import) | Unchanged; import per SSRQ |
| Diagnostics | `getDiagnostics` (IDE bridge) | — | None (no LSP source) | Empty stub, documented |
| File open | `openFile` (IDE bridge) | fs RPCs (app-server clients) | File panes | Bridge (B) / adapter (A) |

## Notes
- Full research + design: Warp plan `72204e45-c3d4-419f-b94e-f5f5b045a480` (Tier B primary, Tier A optional).
- Research anchors: `warpdotdev/claude-code-warp` + `warpdotdev/codex-warp` (OSC 777 hooks; one-directional);
  `coder/claudecode.nvim` PROTOCOL.md (IDE bridge spec); `openai/codex` app-server README + OpenAI's "Unlocking the
  Codex harness" (2026-02); `hoveychen/cc-adapter` reverse-engineering (native VS Code extension = stream-json host).
- Auth/ToS: Relay invokes the user's own local CLI with the user's own credentials (same as any terminal). If Relay is
  ever distributed: Anthropic discourages third-party products routing Pro/Max subscription credentials through an
  Agent-SDK-style host; OpenAI asks integrations to set `clientInfo.name` (enterprise register).
