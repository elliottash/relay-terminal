# Relay documentation

Start with [ARCHITECTURE.md](ARCHITECTURE.md) for how Relay works today and
[ROADMAP.md](ROADMAP.md) for the public development overview.

## Current

| Document | What it covers |
|---|---|
| [DEBUG-HYGIENE.md](DEBUG-HYGIENE.md) | Local diagnostic summaries, outcome/origin coverage, private daily snapshots and weekly signal/card review (#HG26) |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Process model, panes, keyboard, routing, inline agent output, control, file panes, agent backend, keys, isolation, packaging layout, engine interface |
| [ROADMAP.md](ROADMAP.md) | Public development overview and links to the Board |
| [CRASH-DIAGNOSIS.md](CRASH-DIAGNOSIS.md) | What to do when Relay dies: telling a crash from a quit, the `gui_crash` report in relay.log and `worker-faults.log`, `scripts/relay-debug`, reproducing it outside the app, and why this machine keeps no cores |
| [PROFILING.md](PROFILING.md) | Machine setup for the test-suites and profiling tooling (`scripts/relay-tooling-setup`), a second runner on any machine you name (`scripts/relay-remote-tests`), the three profile targets by hand (build, Python tests, app), viewing a profile without QtWebEngine, and where results go |
| [VALIDATION.md](VALIDATION.md) | Test inventory, what was verified live, what was not, QA lane status |
| [RELEASING.md](RELEASING.md) | How to cut a Linux beta: `.deb`s, AUR, checksums, GitHub Pages, version scheme |
| [QUEUE-INTERRUPT.md](QUEUE-INTERRUPT.md) | Agent prompt queue and interrupt protocol; design for queuing shell commands |
| [KEYBINDING-PRESETS.md](KEYBINDING-PRESETS.md) | Warp, VS Code and Konsole shortcut presets and how they map to Relay actions |
| [F-KEYS.md](F-KEYS.md) | Proposal: what belongs on an F-key versus a Ctrl chord, which four keys to assign, and what each would cost vim, nano and mc |
| [ENGINE.md](ENGINE.md) | Relay's own terminal engine: cores, PTY, view, `TerminalBackend`, status and plans (spike history folded in) |
| [ENGINE-PERF.md](ENGINE-PERF.md) | Terminal engine and emulator-core benchmarks (2026-09-17 measurements) |
| [AGENT-SESSIONS-PROTOCOL.md](AGENT-SESSIONS-PROTOCOL.md) | GUI ↔ worker message contract: sessions, planning, subagents, suggestions, model roles and tiers, the Board, SSH, guest agents, questions, local model servers |
| [MCP.md](MCP.md) | MCP servers: global and per-project config, per-server trust (trusted runs, untrusted asks), import from Claude Code / Codex / Warp, and why guests reach them through `relay_board` (`#SSRQ`) |
| [TASK-PLUGINS.md](TASK-PLUGINS.md) | Task plugins: the versioned `plugin.json` a workspace kind declares (router, runner, tools, skills, panes, preview, required programs), its three origins, and why a cloned project cannot enable its own code (`#C0Q8`) |
| [LOCAL-MODELS.md](LOCAL-MODELS.md) | Running the agent on a model served on this machine: the endpoint registry, probing, the `local` tier and role, and the `local_*` messages (`#24XJ`) |
| [SSH-AND-MOSH.md](SSH-AND-MOSH.md) | How Relay wraps `ssh` and `mosh` in its pane shells, per-host policy, connection sharing, and what a remote pane can do (`#S5SH`) |
| [THEMES.md](THEMES.md) | Theme design rather than the theme system: the rules a theme must follow, how contrast is measured, and the Dark Copper / IBM Beige audition (`#0JA7`) |
| [RELAY-FREE.md](RELAY-FREE.md) | Relay Free: the included hosted allowance for a fresh install — identity, token, gateway contract, quotas, privacy posture, operating notes and what exists so far (`#HG7K`) |
| [RELAY-FREE-HANDOFF.md](RELAY-FREE-HANDOFF.md) | Handoff for the three open Relay Free items: the allowance chip in the phone and Relay-to-Relay views, tuning the Lite output cap from measured replies, and moving Lite to Gemini direct (`#HG7K`) |
| [REMOTE-PROTOCOL.md](REMOTE-PROTOCOL.md) | RRP/1: the wire contract for phone access and multiplayer — Noise handshake, pairing, messages, sequencing, the rendezvous API, and what is built so far |
| [PROJECT-INIT-AND-IMPORT.md](PROJECT-INIT-AND-IMPORT.md) | What Relay finds in a project offline before it offers to make a Board, the seven trackers it can import, how an item becomes a card, and the `project_probe` / `board_import_*` messages |
| [GITHUB-SYNC.md](GITHUB-SYNC.md) | Two-way sync between shared work cards and GitHub issues: the mapping, the three-way merge, conflicts, the privacy guard, credentials, rate limits, and the `forge_sync_*` messages |

## Research (inputs to decisions; may describe earlier behavior)

| Document | What it covers |
|---|---|
| [NEXT-STEPS-RESEARCH.md](NEXT-STEPS-RESEARCH.md) | Konsole fork vs. owned engine, per-pane process isolation, terminal-only Relay |
| [DISTRIBUTION-RESEARCH.md](DISTRIBUTION-RESEARCH.md) | Public pointer to current build and release instructions |
| [CONTROL-AND-FILE-PANES-RESEARCH.md](CONTROL-AND-FILE-PANES-RESEARCH.md) | How Warp and others hand control between human and agent, password detection, file pane options |
| [INTAKE-CLARIFICATION-RESEARCH.md](INTAKE-CLARIFICATION-RESEARCH.md) | Recaps, command suggestions, @ files, compaction thresholds, background handoff, instruction and agent file conventions, full-screen and input detection |
| [SCRATCHPAD-DESIGN.md](SCRATCHPAD-DESIGN.md) | Proposed per-project user–agent scratchpad: format, review loop, actions, merge rules, protocol |
| [MEMORY-AND-MULTI-REQUEST-RESEARCH.md](MEMORY-AND-MULTI-REQUEST-RESEARCH.md) | How Claude Code, Codex, opencode and Warp keep track of multiple requests and long conversations; Relay drop paths and fixes |
| [BOARD-DESIGN.md](BOARD-DESIGN.md) | The Board, v2: the board folder as Trello-like cards with agent threads, git format, agent tools and autonomy, conversion, phases |
| [TASKS-AND-MEMORY-DESIGN.md](TASKS-AND-MEMORY-DESIGN.md) | Agent task list (todos, card checklist items, cards) and project memory in the Board: Claude Code and Warp compared, formats, tools, UI, phases |
| [BOARD-FORMAT.md](BOARD-FORMAT.md) | The Board file format reference: the board folder (`board/`, or the `.switchboard/`, `switchboard/` or `issues/` a project already has), card front matter per type, task markers and their `blocked_by=`, threads, ids, ranks, board.yaml, `relay-board.py` check/index/migrate |
| [TERMINAL-ENGINE-OPTIONS.md](TERMINAL-ENGINE-OPTIONS.md) | Permissively licensed, cross-platform terminal cores and widgets compared (libghostty-vt, Contour, alacritty_terminal, xterm.js, libvterm, GPL references) |
| [AGENT-FEATURES-RESEARCH.md](AGENT-FEATURES-RESEARCH.md) | Session, planning, model/effort, command and subagent UX in Warp, opencode, Claude Code and Codex; recommendations and a subagent design for Relay |
| [TOKEN-EFFICIENCY-HARNESSES-RESEARCH.md](TOKEN-EFFICIENCY-HARNESSES-RESEARCH.md) | Codex, OpenCode V2 and Claude Code context/cost controls compared with Relay; measured usage spike and prioritized token-efficiency proposals |
| [PALETTE-RESEARCH.md](PALETTE-RESEARCH.md) | Palette designs in other tools; recommended two palettes, later merged into one actions palette, since 2026-09-18 the Actions tab and search of the Settings pane |
| [REMOTE-AND-MULTIPLAYER-DESIGN.md](REMOTE-AND-MULTIPLAYER-DESIGN.md) | Public pointer to the implemented remote protocol and source |
| [OPENCODE-NOTES.md](OPENCODE-NOTES.md) | What Relay's agent could adopt from opencode, ranked |
| [SWITCHBOARD-AESTHETIC.md](SWITCHBOARD-AESTHETIC.md) | Proposal: the switchboard aesthetic inside Relay — design only, nothing implemented (`#8E4Q`) |
| [BOARD-TOOLING-RESEARCH.md](BOARD-TOOLING-RESEARCH.md) | The Board as the project's tooling hub (#7BM4): test explorers and test analytics, what "stale", "slow" and "flaky" mean and how they are measured, profiling shapes and what is feasible on these machines, how seven agent products surface verification, and a ranked list of other tooling that fits a board of cards |
| [QA-ACROSS-FIELDS-RESEARCH.md](QA-ACROSS-FIELDS-RESEARCH.md) | How quality is assured where code is involved — games and apps, CLIs, subsystems and services, estimators and data pipelines, papers, reports and lab work — reduced to one idea (an asymmetry between maker and checker, and the right judge), the lines every field draws, five QA shapes a card can carry, and a phasing that keeps it light (#YZ8G). The five source reports are under `research/qa-across-fields/` |
| [GLOBAL-PROJECT-BOARD-RESEARCH.md](GLOBAL-PROJECT-BOARD-RESEARCH.md) | Product research for a Relay-wide Projects feature (#V3R3): a portfolio above per-project Boards, criticality, optional budgets, cross-project work, linked roots, read-time rollups, privacy, and a phased implementation. Three source reports are under `research/global-project-board/`. |
| [RESEARCH.md](RESEARCH.md) | Historical: primary sources checked for the first build (KonsolePart, Qt editor, Warp licensing, Kimi and Z.AI endpoints) |

## Evidence

[`qa_evidence/`](qa_evidence/) holds screenshots, dumps and driver scripts, one folder per
feature (`YYYY-MM-DD-slug/`). Files named `implementer-*` come from the implementing session,
not from independent QA. A **partial** index of folders is in
[VALIDATION.md](VALIDATION.md#verified-live-implementer-checks) — it stops at 2026-09-18 and
names about a quarter of the folders, so the directory listing is the complete one.

## Issues

The tracker lives in [`../issues/`](../issues/README.md).
