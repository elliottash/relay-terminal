# Relay documentation

Start with [ARCHITECTURE.md](ARCHITECTURE.md) for how Relay works today and
[ROADMAP.md](ROADMAP.md) for where it is going.

## Current

| Document | What it covers |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | Process model, panes, keyboard, routing, inline agent output, control, file panes, agent backend, keys, isolation, packaging layout, engine interface |
| [ROADMAP.md](ROADMAP.md) | Goals, decisions already made, near/mid/longer-term work with issue links, non-goals |
| [VALIDATION.md](VALIDATION.md) | Test inventory, what was verified live, what was not, QA lane status |
| [RELEASING.md](RELEASING.md) | How to cut a Linux beta: `.deb`s, AUR, checksums, GitHub Pages, version scheme |
| [QUEUE-INTERRUPT.md](QUEUE-INTERRUPT.md) | Agent prompt queue and interrupt protocol; design for queuing shell commands |
| [KEYBINDING-PRESETS.md](KEYBINDING-PRESETS.md) | Warp, VS Code and Konsole shortcut presets and how they map to Relay actions |
| [ENGINE-SPIKE.md](ENGINE-SPIKE.md) | libvterm + QPainter terminal spike: results, gaps vs. KonsolePart, estimates |

## Research (inputs to decisions; may describe earlier behavior)

| Document | What it covers |
|---|---|
| [NEXT-STEPS-RESEARCH.md](NEXT-STEPS-RESEARCH.md) | Konsole fork vs. owned engine, per-pane process isolation, terminal-only Relay |
| [DISTRIBUTION-RESEARCH.md](DISTRIBUTION-RESEARCH.md) | Linux packaging options, macOS and Windows blockers and signing costs, website, phased plan |
| [CONTROL-AND-FILE-PANES-RESEARCH.md](CONTROL-AND-FILE-PANES-RESEARCH.md) | How Warp and others hand control between human and agent, password detection, file pane options |
| [INTAKE-CLARIFICATION-RESEARCH.md](INTAKE-CLARIFICATION-RESEARCH.md) | Recaps, command suggestions, @ files, compaction thresholds, background handoff, instruction and agent file conventions, full-screen and input detection |
| [SCRATCHPAD-DESIGN.md](SCRATCHPAD-DESIGN.md) | Proposed per-project user–agent scratchpad: format, review loop, actions, merge rules, protocol |
| [MEMORY-AND-MULTI-REQUEST-RESEARCH.md](MEMORY-AND-MULTI-REQUEST-RESEARCH.md) | How Claude Code, Codex, opencode and Warp keep track of multiple requests and long conversations; Relay drop paths and fixes |
| [SWITCHBOARD-DESIGN.md](SWITCHBOARD-DESIGN.md) | Board (Switchboard) v2: issues/ as Trello-like cards with agent threads, git format, agent tools and autonomy, conversion, phases |
| [TASKS-AND-MEMORY-DESIGN.md](TASKS-AND-MEMORY-DESIGN.md) | Agent task list (todos, card checklist items, cards) and project memory in the Switchboard: Claude Code and Warp compared, formats, tools, UI, phases |
| [TERMINAL-ENGINE-OPTIONS.md](TERMINAL-ENGINE-OPTIONS.md) | Permissively licensed, cross-platform terminal cores and widgets compared (libghostty-vt, Contour, alacritty_terminal, xterm.js, libvterm, GPL references) |
| [AGENT-FEATURES-RESEARCH.md](AGENT-FEATURES-RESEARCH.md) | Session, planning, model/effort, command and subagent UX in Warp, opencode, Claude Code and Codex; recommendations and a subagent design for Relay |
| [PALETTE-RESEARCH.md](PALETTE-RESEARCH.md) | Palette designs in other tools; recommended two palettes, later merged into one actions palette |
| [OPENCODE-NOTES.md](OPENCODE-NOTES.md) | What Relay's agent could adopt from opencode, ranked |
| [RESEARCH.md](RESEARCH.md) | Historical: primary sources checked for the first build (KonsolePart, Qt editor, Warp licensing, Kimi and Z.AI endpoints) |

## Evidence

[`qa_evidence/`](qa_evidence/) holds screenshots, dumps and driver scripts, one folder per
feature (`YYYY-MM-DD-slug/`). Files named `implementer-*` come from the implementing session,
not from independent QA. The index of folders is in [VALIDATION.md](VALIDATION.md#verified-live-implementer-checks).

## Issues

The tracker lives in [`../issues/`](../issues/README.md).
