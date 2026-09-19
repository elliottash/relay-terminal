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
- [ ] Live tail of the active pane's guest transcript (`guest_sessions.LiveTail` has no caller; a running session's row refreshes on the next reconcile instead) <!-- t:t1 -->
- [ ] Validation: scripts/test.sh + ctest + Xvfb live run; QA evidence <!-- t:bt -->
- [x] Tier A headless harness adapters: the contract (`guest_harness.py`), `codex app-server` and `claude -p` stream-json adapters with recorded fixtures, the worker's `HarnessProvider` and `guest:` presets, the picker routed to the preset when usable (protocol §29). Un-deferred by the owner on 2026-09-19 ("i wanted Tier A now … go ahead and unlock that now") <!-- t:x2 -->
- [x] Tier A follow-ups, the code half: streaming tool output (`tool_output`; codex streams it, Claude Code's stream-json drops the text and that is written down), the context window in `usage`, and scoped approvals (`once` / `session` / `stop`, both guests) <!-- t:a3 -->
- [ ] Tier A follow-ups, the owner's half: whether Tier B's hook and statusline entries should travel with a headless claude, whether to show a list-price `cost_usd` for a subscription guest, Codex's `--remote` co-attach (it attaches; nothing is built on it), and claude's `tool_progress` (elapsed seconds per running tool, which would need a new event kind and a pane that renders it) <!-- t:a4 -->
- [x] Picker route: "Claude Code" / "Codex" rows in the pane's model box (`guest:<id>`, no tier, no key), `/model claude|codex`, launched by the pane in its own shell (§26.9) <!-- t:pk -->
- [x] Launch-time configuration instead of installed files: `guest_launch.py` writes `<runtime>/guest/claude-settings.json` for `claude --settings`, `-c` overrides for codex, bypass flags on both, bridge variables on the command line only; flags verified against Claude Code 2.1.278 / Codex 0.155.1 <!-- t:c1 -->
- [x] Retire the setup surface: Options › Guests rows and the `guest_install` / `guest_codex --enable` command lines gone; libraries kept for the legacy cleanup every launch runs <!-- t:rs -->
- [x] Bridge always on: no setting; sidecar starts with the first claude launch, stops 60 s after the last claude leaves <!-- t:ba -->
- [x] Composer delivery in a guest pane: terminal verdict → `!<command>` into the guest, agent verdict → the prompt; Relay's UI unchanged (§26.8) <!-- t:cd -->
- [x] Sessions resume/fork go through `launchGuest` (same settings, flags and bridge as a pick), in the session's cwd, this pane or a new one <!-- t:s2 -->
- [x] Codex parity written down honestly (§26.6): status, busy, notify, sessions, bypass yes; diffs in Relay no; Codex 0.155.1 hooks (Claude's schema, trust-gated) documented, not wired <!-- t:cp -->
- [x] A guest turn running when the pane switches model: the pane shifts over at once like a native model switch, the guest's turn finishes and it is then asked to `/exit` (owner, 2026-09-19; `20f9e53`) <!-- t:q9 -->

## Decisions
- 2026-09-19, owner: translator-in-pane first ("i dont need a headless protocol… a special translator from the relay
  prompt into the claude code input and back"); headless/app-server adapters deferred to a later optional phase.
- 2026-09-19, owner: keep the terminal/agent/auto-detect system; where Relay lacks a guest capability, Relay adopts it
  as / commands (guest badge in the slash popup, pass-through to the guest).
- 2026-09-19, owner: no screen scraping for state — structured sources only (IDE bridge, hooks, statusline JSON,
  transcript/rollout tailing, app-server daemon).
- 2026-09-19, agent: Relay implements the same protocols VS Code uses — the claude IDE bridge (lock file + WebSocket
  MCP) now, and claude stream-json + codex app-server as a client in the deferred Tier A phase.
- 2026-09-19, owner (second round): **no per-project setup** — the Options › Guests install rows were a stopgap, not the
  product; full integration through the IDE bridge; claude / codex selectable from the pane's model selector like any
  model, running in the pane's own shell and cwd; a guest diff is decided in the diff view.
- 2026-09-19, owner: "the pane content itself also looks the same as regular relay — a terminal system. relay terminal
  commands are piped to the agent as '! …' to maintain a seamless / identical experience"; "the claude / codex agent
  needs to understand an auto-detected terminal command or agent prompt." Relay's UI does not change in a guest pane;
  only delivery does (§26.8).
- 2026-09-19, owner: "the claude / codex agent needs to be --yolo / --dangerously-skip-permissions to allow moving
  around the file system, like relay / warp does." Both launches carry the bypass flag; the permission bar stays for a
  hand-started claude whose own settings ask.
- 2026-09-19, owner: Tier A (t:x2) un-deferred — "i think i want to undefer and start woking on it"; what it unlocks is
  written in the t:x2 task and in §26.6's parity paragraph (Codex diffs in Relay need it).
- 2026-09-19, owner: "a busy guest model change should be the same as our relay-native models. just shift over
  immediately." The refusal the agent had shipped is gone: the pick applies at once, the guest's turn in flight
  finishes, then it is asked to `/exit` (§26.9 "Leaving a guest").

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
- `backend/relay_core/guest_launch.py` — **the picker route (§26.9)**: per launch, the claude settings file under the
  pane's runtime dir (`relay_entries()` verbatim, the user's own statusline kept), the claude / codex command lines
  with the bypass flags and the bridge variables, and the legacy cleanup of the stopgap's marked entries. Verified
  flags: Claude Code 2.1.278 `--settings`, `--dangerously-skip-permissions`, `-r`, `--fork-session`; Codex 0.155.1
  `-c key=value` (plain, `resume`, `fork`), `--dangerously-bypass-approvals-and-sandbox`.
- `backend/relay_core/guest_hook.py` + `guest_install.py` — the claude hook/statusline shim, and the entries it is
  reached by (`relay_entries()`, the `--relay-guest` marker). The installer's command line is **retired**; `remove()`
  stays as the launch's legacy cleanup and `install()` only as its tested inverse.
- `backend/relay_core/guest_codex.py` — the `notify` hook (now a `-c` override on the launch, no file), the rollout tail
  under `~/.codex/sessions/YYYY/MM/DD/`, and the retired TOML settings writer kept for the legacy cleanup.
- `backend/relay_core/guest_bridge.py` + `src/GuestBridge.h` — the Claude IDE bridge sidecar (loopback WebSocket
  MCP, `~/.claude/ide/<port>.lock`, the 12 IDE tools) and its GUI half. `openDiff` blocks on the user's decision
  in Relay's diff view: `FILE_SAVED` (the sidecar writes, never the GUI) or `DIFF_REJECTED`; every path must
  realpath-resolve inside the one pane's workspace. `guest.diffSave` accepts from the keyboard.
- `backend/relay_core/guest_sessions.py` — the `claude` / `codex` Conversations sources and `reconcile()`;
  records carry the tool's own `resume_command`; rename/pin/delete are index-only. Wired into
  `session_protocol._conversations()` on 2026-09-19: the listing is answered from the index and the
  reconcile runs behind it on a worker thread, guest ids are accepted where Relay's 32-hex shape is
  not, and the Sessions pane's Kind filter lists both guests (`src/Conversations.cpp`,
  `src/Pane.h`'s `openGuestSession`, `RelayWindow::openGuestPane`).
- `backend/relay_core/guest_slash.py` — the static slash catalogs (claude built-ins + skills + legacy commands;
  codex TUI set), published live as `slash` events.
- `src/Pane.h` — `pollGuestEvent` dispatch, guest state in `program_state` (`guest_model`, `guest_context_pct`,
  `guest_busy`), the `/` popup's badged guest rows, the openDiff banner, composer → guest delivery (§26.8), and the
  picker route: the guest rows in the model box, `chooseGuest` / `launchGuest` / `leaveGuest`, `/model claude|codex`.
- `src/RelayWindow.h` — `openGuestPane(source, guest, extra, cwd)`: a session resumed in a new pane launches there.
  Settings › Guests is gone.
- `remote/wire.py` — `GUEST_CHANNEL_EVENTS`: all five guest event kinds are local-only, written down
  beside the worker lists rather than inside them, one regression test.
- Tests: `tests/test_guest{,_hook,_install,_codex,_bridge,_sessions,_slash}.py`, `tests/guestbridge_test.cpp`,
  the wire withholding regression in `tests/test_remote_wire.py`.

As-built deviations from the plan, all recorded in §26 as required: no codex daemon spike (the rollout tail
covers Tier B); the bridge is one sidecar per GUI run rather than per pane; module names are singular
(`guest_hook.py`); the bridge folds into the hooks phase's single channel writer rather than keeping its own.

## Evidence

`docs/qa_evidence/2026-09-19-claude-codex-guest-integration/` — per track: claude-hooks (5 Xvfb shots incl.
the PreToolUse question bar), claude-bridge (4 Xvfb shots of the openDiff banner / save / reject), options
(5 Xvfb shots of the Guests rows — **historical**: the section was retired on 2026-09-19), composer
(`composer-evidence.md`), sessions backend (`sessions-index-evidence.md` + real-data measurements: 77 claude
sessions indexed in 1.19 s, warm reconcile 1 ms), and the picker route (`picker-README.md`: 6 Xvfb shots from
`picker-drive.py` with a stand-in `claude`, and `launch-flags-README.md`: the real CLIs' flags exercised).
Tier A (§29): `harness-claude-README.md` and `harness-codex-README.md` (the adapters against the real CLIs,
recorded into `tests/fixtures/guest_harness_*`), and `harness-drive-README.md` (4 Xvfb shots from `harness-drive.py`:
Claude Code picked from the box, one real turn answered in Relay's own transcript, the shell still the pane's).
Implementer shots are prefixed `implementer-` and are not QA verdicts. Each track's drive script and run log sits
beside its shots.

## QA checklist

- [ ] `./scripts/test.sh` and `ctest --test-dir build` pass on the final tree, including the guest suites named
      above and the wire withholding regression.
- [ ] "Claude Code" and "Codex" are rows of the pane's model box when the CLIs are on PATH; picking one (or
      `/model claude`) types a visible `claude --settings <runtime>/guest/claude-settings.json
      --dangerously-skip-permissions` line (with `CLAUDE_CODE_SSE_PORT` / `ENABLE_IDE_INTEGRATION` prefixed) into
      the pane's own shell; the settings file holds the four hook entries and a `statusLine` only when the user has
      none of their own; nothing under the project's `.claude/` or `~/.codex/` is written.
- [ ] With the guest running, the box shows its row; a terminal-mode line (or a typed `!`) reaches the guest as
      `!<command>`, an agent-mode line as the prompt; picking a preset while the guest is idle types `/exit` and
      then switches; while it is busy the switch is refused with a status line.
- [ ] With Claude Code in a pane, the pane shows the guest chip (model, context %) from the statusline shim while
      claude still renders its own statusline.
- [ ] A PreToolUse hook surfaces as a Relay question bar on the pane; it is never auto-approved; answering it
      lets the claude turn proceed with the user's decision.
- [ ] A claude edit arrives as openDiff: Relay's diff view with the "claude proposes changes to …" banner;
      Save writes the file (claude sees FILE_SAVED), Reject answers DIFF_REJECTED; `guest.diffSave` saves from
      the keyboard; a diff naming a path outside the pane's workspace is rejected.
- [ ] A project or home still carrying the retired installer's `--relay-guest` entries has exactly those removed at
      the first launch (the pane says which files), and the user's own entries in those files are untouched.
- [ ] Picking Codex types `codex -c notify=[…] -c tui.notification_condition="always"
      --dangerously-bypass-approvals-and-sandbox`; a finished codex turn reaches Relay's notification centre with no
      entry in `~/.codex/config.toml`.
- [ ] In a guest pane the `/` popup lists the guest's commands badged as the guest's; choosing one types it into
      the guest (queued as "Queued · sent to <guest> when it is ready" while the guest is busy).
- [ ] The sessions pane lists claude and codex sessions beside Relay's own with titles and dates; Enter resumes
      in the focused pane and Shift+Enter in a new one via the tool's own `resume_command`; rename/pin/delete
      leave the files under `~/.claude` and `~/.codex` untouched; a running session's transcript updates live.
- [ ] With a pane shared to a remote host, no guest event kind and no `guest_*` program-state field crosses the
      wire.
- [ ] Tier A (§29): with `claude` on PATH the model box lists the worker's "Claude Code" row (no Tier B duplicate);
      picking it (or `/model claude`) configures the worker on `guest:claude` with no TUI in the terminal; a prompt
      runs one headless turn and Relay's transcript prints the answer and each tool call as a call line with its
      diff; a terminal-mode line runs in the pane's own shell; picking a normal preset closes the harness and keeps
      the conversation; a guest that cannot start leaves the pane on its previous model.
- [ ] Tier A: the same for Codex (`codex app-server`), and a Codex file change arriving as a call line with the patch
      as its diff; no approval is raised under the bypass posture.
