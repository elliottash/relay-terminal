---
id: GT7X
type: work
status: needs-qa-llm
labels: [feature]
component: [worker, gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 4.8 (Warp Oz orchestrator + six child agents), 2026-09-19
rank: zzzzzz
created: '2026-09-19'
acceptance: claude or codex running in a pane gets composer routing with guest slash autocomplete, pane status and notifications from hooks, guest sessions in the sessions pane with resume, and claude openDiff rendered in a Relay diff view
source: 'conversation, 2026-09-19: "research how warp has the plugins to nicely interact with claude and codex… plan a similar feature for relay terminal"'
links: {plans: [], commits: [d166332, 467138f, b455dae, ca860a3, b2af76c, 0160685, 7b83902, b98d4b0, 274c42e], evidence: [docs/qa_evidence/2026-09-19-claude-codex-guest-integration/], related: [SSRQ], github: null}
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
- [x] Foundation: guest registry, foreground detection of claude/codex, pane guest state <!-- t:t7 -->
- [x] Claude IDE bridge (WebSocket MCP `ide` server, `~/.claude/ide` lock file, 12 tools, openDiff → Relay diff view) <!-- t:cc -->
- [x] Claude hooks + statusline shim (notifications, pane states, context/model chips) <!-- t:gw -->
- [x] Codex Tier B: ~~daemon co-attach spike~~ → marked settings + rollout tail (spike declined: the tail covers Tier B; daemon stays Tier A) <!-- t:yz -->
- [x] Composer translator + slash registry (guest autocomplete, TUI-state-aware injection) <!-- t:sy -->
- [x] Sessions pane: claude + codex conv_index sources, resume/fork, unified search <!-- t:2f -->
- [ ] Validation: scripts/test.sh + ctest + Xvfb live run; QA evidence <!-- t:bt -->
- [x] ~~Tier A headless harness adapters (codex app-server first, then claude stream-json)~~ — deferred by owner, kept as later optional phase <!-- t:x2 -->

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

## Where it is

Built by six child-agent tracks off `feature/guest-agents`, merged in order: foundation → sessions backend →
claude hooks → codex → composer → claude bridge → Options › Guests → sessions UI. The binding spec is
`docs/AGENT-SESSIONS-PROTOCOL.md` section 26 (contracts §26.3–26.8, written before the phases); the architecture
write-up is `docs/ARCHITECTURE.md` section 11a.

- `backend/relay_core/guest.py` — the registry: `GuestSpec`s, `classify_command`, `detect_installations`,
  `bridge_env`. Mirrored in C++ where the pane classifies its foreground process (one rule, two languages).
- `shell/guest-event.py` — the one channel writer (§26.3): every guest phase reaches its pane through the
  `{"token", "sequence", "event", "guest", "data"}` envelope, one file per event in the pane's
  `guest-events/` spool (`mkstemp` + `os.replace`), no-op without `RELAY_GUEST_EVENT`.
- `backend/relay_core/guest_hook.py` + `guest_install.py` — the claude hook/statusline shim and its marked,
  additive installer for `.claude/settings.json` (`--relay-guest` marker; off removes exactly the marked entries;
  a user's own statusline is kept). PreToolUse is a Relay question on the pane, never auto-approved.
- `backend/relay_core/guest_codex.py` — the marked `notify` + `[tui] notification_condition` entries in
  `~/.codex/config.toml` (byte-for-byte TOML document model; user-owned keys are a hard `SettingsConflict`) and
  the rollout tail under `~/.codex/sessions/YYYY/MM/DD/`.
- `backend/relay_core/guest_bridge.py` + `src/GuestBridge.h` — the Claude IDE bridge sidecar (loopback WebSocket
  MCP, `~/.claude/ide/<port>.lock`, the 12 IDE tools) and its GUI half. `openDiff` blocks on the user's decision
  in Relay's diff view: `FILE_SAVED` (the sidecar writes, never the GUI) or `DIFF_REJECTED`; every path must
  realpath-resolve inside the one pane's workspace. `guest.diffSave` accepts from the keyboard.
- `backend/relay_core/guest_sessions.py` — the `claude` / `codex` Conversations sources and `reconcile()`;
  records carry the tool's own `resume_command`; rename/pin/delete are index-only.
- `backend/relay_core/guest_slash.py` — the static slash catalogs (claude built-ins + skills + legacy commands;
  codex TUI set), published live as `slash` events.
- `src/Pane.h` — `pollGuestEvent` dispatch, guest state in `program_state` (`guest_model`, `guest_context_pct`,
  `guest_busy`), the `/` popup's badged guest rows, the openDiff banner, composer → guest input routing.
- `src/RelayWindow.h` — Settings › Guests, the two installers' front end (status re-read on show; JSON answers;
  no reset rows; PYTHONPATH appended, never prepended).
- `remote/wire.py` — `WITHHELD_EVENTS`: all five guest event kinds are local-only, one regression test.
- Tests: `tests/test_guest{,_hook,_install,_codex,_bridge,_sessions,_slash}.py`, `tests/guestbridge_test.cpp`,
  the wire withholding regression in `tests/test_remote_wire.py`.

As-built deviations from the plan, all recorded in §26 as required: no codex daemon spike (the rollout tail
covers Tier B); the bridge is one sidecar per GUI run rather than per pane; module names are singular
(`guest_hook.py`); the bridge folds into the hooks phase's single channel writer rather than keeping its own.

## Evidence

`docs/qa_evidence/2026-09-19-claude-codex-guest-integration/` — per track: claude-hooks (5 Xvfb shots incl.
the PreToolUse question bar), claude-bridge (4 Xvfb shots of the openDiff banner / save / reject), options
(5 Xvfb shots of the Guests rows incl. the global guard and a codex SettingsConflict), composer
(`composer-evidence.md`), sessions backend (`sessions-index-evidence.md` + real-data measurements: 77 claude
sessions indexed in 1.19 s, warm reconcile 1 ms). Implementer shots are prefixed `implementer-` and are not QA
verdicts. Each track's drive script and run log sits beside its shots.

## QA checklist

- [ ] `./scripts/test.sh` and `ctest --test-dir build` pass on the final tree, including the guest suites named
      above and the wire withholding regression.
- [ ] With Claude Code in a pane and "Claude Code in this project" on, the pane shows the guest chip (model,
      context %) from the statusline shim while claude still renders its own statusline.
- [ ] A PreToolUse hook surfaces as a Relay question bar on the pane; it is never auto-approved; answering it
      lets the claude turn proceed with the user's decision.
- [ ] A claude edit arrives as openDiff: Relay's diff view with the "claude proposes changes to …" banner;
      Save writes the file (claude sees FILE_SAVED), Reject answers DIFF_REJECTED; `guest.diffSave` saves from
      the keyboard; a diff naming a path outside the pane's workspace is rejected.
- [ ] Options › Guests: the project toggle writes only `--relay-guest`-marked entries to `.claude/settings.json`
      and off removes exactly those; the global toggle is refused unless the project one is on; a user's own
      statusline in the file is kept and reported as kept.
- [ ] The Codex toggle writes the marked `notify` + `[tui] notification_condition` entries to
      `~/.codex/config.toml`; on→off returns the file byte for byte; a user-owned `notify` produces the
      SettingsConflict notice and no write. A finished codex turn reaches Relay's notification centre.
- [ ] In a guest pane the `/` popup lists the guest's commands badged as the guest's; choosing one types it into
      the guest (queued as "Queued · sent to <guest> when it is ready" while the guest is busy).
- [ ] The sessions pane lists claude and codex sessions beside Relay's own with titles and dates; Enter resumes
      in the focused pane and Shift+Enter in a new one via the tool's own `resume_command`; rename/pin/delete
      leave the files under `~/.claude` and `~/.codex` untouched; a running session's transcript updates live.
- [ ] With a pane shared to a remote host, no guest event kind and no `guest_*` program-state field crosses the
      wire.
