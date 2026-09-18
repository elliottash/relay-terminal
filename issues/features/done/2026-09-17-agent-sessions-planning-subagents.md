---
id: KJ44
type: work
status: done
labels: [feature]
component: [agent, worker, gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (orchestrating) with subagents, 2026-09-17
rank: '15'
created: '2026-09-17'
acceptance: per-workstream issues filed by the implementing agents, each with implementer evidence and a QA checklist
source: '`docs/AGENT-FEATURES-RESEARCH.md` recommendations and `issues/feature_intake.txt` (2026-09-17): "how to open files by typing their names, maybe @? any file that can be previewed in a pane will then start showing up with a type text filter."; "command suggestion (see how warp / claude do this)"; "add claude style recaps"; "allow queueing of terminal commands and agent commands -- make them colored or show up differently in the queue eg a different icon."; "dont hide the prompt box when any program is running, eg sudo apt upgrade, so i can queue agent / terminal commands. i would say, only hide the prompt box for full screen apps and for password input."'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Agent sessions, planning, instructions, subagents, suggestions and unified queue

## Owner decisions (2026-09-17)

1. **Plan mode like Warp:** the agent can still run commands to investigate, then writes the plan as Markdown that opens in a text-editable pane. Plans are saved in `<project>/.relay/plans` (changeable in agent options). No approvals during planning is accepted.
2. **Instruction files:** on first launch Relay scans for Claude Code, Codex, Warp and other tools' instruction files, asks which to include (checkboxes) or offers to create a global `~/.config/relay/relay.md` synthesized from them; changeable later in agent options. Project files in the workspace are included automatically.
3. **Compaction** triggers at a threshold of the selected model's context window (default pending research, placeholder 90%).
4. **Background subagents** that finish while the main agent is idle start a main-agent turn with the result.
5. **Agent definitions:** Relay uses agent files from all tools' locations.
6. **@ files:** `@name` at the start of an empty prompt opens the file in a preview pane; inside an agent prompt it attaches the file. Fuzzy picker, previewable files first, gitignore respected.
7. **Command suggestions:** all three: ghost-text history autosuggestions, AI-suggested next command after a command, and suggested next prompts.
8. **Recaps:** session-return summaries (when resuming a session or coming back after the agent worked while you were away).
9. **One combined queue** of terminal commands and agent prompts, run in the order entered, reorderable with Ctrl+Up/Down, visually distinct (color and icon).
10. **Prompt box visibility:** only hide it for full-screen apps and password input; it stays visible for programs like `sudo apt upgrade`. When a running program is waiting for input (e.g. apt's `[Y/n]`), Relay shows a hint and moves focus to the terminal while the prompt stays visible. Supersedes the earlier "all programs hide the prompt" decision.
11. Also from the earlier queue design issue (`2026-09-17-agent-queue-steering-and-editing.md`): Up selects queued items; Enter edits; Esc interrupts while busy; Ctrl+Enter interrupts and sends while busy.

## Protocol

`docs/AGENT-SESSIONS-PROTOCOL.md` (v1).

## Workstreams

| Workstream | Scope | Where |
|---|---|---|
| A backend sessions | model/effort switching, effort mapping, context + compaction, checkpoints/rewind, fork, sessions + recaps, plan mode + write_plan, instructions scan/synthesize/load, attachments, suggestions | worktree, merged by the main session |
| B backend subagents | agent definitions from all tools, agent tool, concurrency, events, messaging, background handoff | worktree |
| D GUI terminal side | combined queue (terminal + agent, ordered, reorder, colors/icons, Up select/Enter edit/Esc/Ctrl+Enter per the queue issue), prompt visibility rules + waiting-for-input focus, @ file picker, ghost-text history suggestions | main tree |
| E GUI agent side | slash menu, context indicator, effort keys, Shift+Tab plan mode + editable plan pane, rewind/fork/resume pickers, recap display, onboarding dialog, subagent list under composer + transcript pane, AI suggestions display | after A, B and D land |

## Owner decisions, intake batch 2 (2026-09-17)

Source lines from `issues/feature_intake.txt` are quoted in the implementing issues. Decisions:

12. **Shortcut hints** Superhuman-style: yes; project `WARP.md` carries a standing rule to add hints for new features.
13. **`!` and `*` prefixes** like Claude Code: a typed `!` as the first character switches to terminal mode, a typed `*` to agent mode; not on paste.
14. **Thinking and tool calls:** approved plan until the new engine (thinking streams in the overlay, one inline "thought for" line, one clickable "N tool calls" line via a registered `relay://` handler opening the turn transcript; tool outputs open in a preview pane). Revisit with the engine.
15. **Routing:** curated list of command names that are common English words plus sentence signals; ambiguous input may call the pane's agent model to guess.
16. **Skills:** refine global skill files into copies; import skill libraries pinned to a commit with review; manual update checks, no subscriptions.
17. **Scratchpad:** yes, a dynamic per-project user–agent comms pad; design research in `docs/SCRATCHPAD-DESIGN.md` (pending).
18. **Panes and tabs:** close and move-to-new-tab buttons on panes; move-to-new-window on tabs; new tab and new pane buttons; drag panes like Warp; Ctrl+Alt+arrows move panes.
19. **Palette aliases:** generous hidden search aliases for Agent options.
20. **Voice transcription:** filed as `2026-09-17-voice-transcription.md` with an open local-vs-cloud decision.

Workstreams: GUI F1 (hints, prefixes, thinking/tool UI, routing assist UI, skills UI, pane/tab buttons, drag/move, aliases), backend F2 (routing assist, thinking/turn/tool-output events, skills refine/import), research (scratchpad design).

## Owner decisions, intake batch 3 (2026-09-17)

21. **Memory and multiple requests:** research how Claude Code, Codex, opencode (and Warp) keep track of multiple requests and long conversations; `docs/MEMORY-AND-MULTI-REQUEST-RESEARCH.md` (pending).
22. **Rewind split:** `rewind-chat` (default; never changes code) and `rewind-code` as separate options (GUI F1).
23. **MVP scope:** delegate/take over, clickable paths, keyboard jump to links, portable engine + screen-text input detection, website and beta, voice transcription (recorded in `docs/ROADMAP.md`).
24. **Scratchpad → board ("Switchboard" candidate name):** Ctrl+Shift+S opens it as a pane; Trello-like movable cards, each card like an issue and a thread with the agent; referable from the terminal thread; tabs with defaults (design, bugs, features, marketing, planning, deferred, done); kept in git for collaborators with a Markdown representation usable without Relay; Relay detects non-compliant issues/notes and converts them; the agent uses it autonomously (iterate with QA). Design v2: `docs/SWITCHBOARD-DESIGN.md` (pending).

## Closed 2026-09-17: where each decision landed

This card recorded the owner's decisions before the work was split; nothing is built against it directly.
Every workstream shipped under its own card, and those hold the implementer evidence and QA checklists.

| Decisions | Card |
|---|---|
| 1 plan mode, 3 compaction threshold, 5–9 sessions/rewind/fork/recaps, instructions scan | `2026-09-17-backend-sessions.md` |
| 2 instruction files, onboarding | `2026-09-17-onboarding-instructions.md` |
| 4 one combined queue, ordering, steering | `2026-09-17-agent-queue-steering-and-editing.md`, `2026-09-17-steering-running-agent-turn.md` |
| 10–11 subagents, automatic-turn limit | `2026-09-17-backend-subagents.md`, `2026-09-17-subagents-ui.md` |
| 12 shortcut hints | `2026-09-17-shortcut-hints.md` |
| 13 `!` and `*` prefixes | `2026-09-17-prefix-modes.md` |
| 14 thinking and tool-call visibility | `2026-09-17-thinking-and-tool-call-summaries.md` |
| 15 routing assist | `2026-09-17-routing-assist-ui.md`, `2026-09-17-routing-assist-thinking-skills-backend.md`, `2026-09-17-router-english-commands.md` |
| 16 skills | `2026-09-17-skills-dialog.md` |
| 17, 24 scratchpad → Switchboard | `docs/SWITCHBOARD-DESIGN.md`, `2026-09-17-switchboard-phase0.md`, phase 1 in flight |
| 18 pane and tab buttons, drag, moves | `2026-09-17-pane-tab-buttons-and-moving.md`, `2026-09-17-pane-move-keys-and-drag-broken.md` |
| 19 palette aliases | `2026-09-17-palette-search-aliases.md` |
| 20 voice transcription | `2026-09-17-voice-transcription.md` |
| 21 memory and multiple requests | `docs/MEMORY-AND-MULTI-REQUEST-RESEARCH.md`, `2026-09-17-request-ledger-todos-completion.md`, `2026-09-17-requests-ui.md` |
| 22 rewind split | `2026-09-17-agent-sessions-ui.md` |
| 23 MVP scope | `docs/ROADMAP.md`; the MVP cards are `#YZTK`, `#GWXM`, `#C1HH`, `#YR21`, `#P4GP`, `#NY7Z` |

The three research documents it was waiting on all exist: `docs/SCRATCHPAD-DESIGN.md`,
`docs/MEMORY-AND-MULTI-REQUEST-RESEARCH.md`, `docs/SWITCHBOARD-DESIGN.md`.
