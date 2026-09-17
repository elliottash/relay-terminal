# Agent sessions, planning, instructions, subagents, suggestions and unified queue

- **Status**: in-progress
- **Component**: agent, worker, gui
- **Milestone**: desktop-alpha
- **Workstream**: agent
- **Acceptance evidence**: per-workstream issues filed by the implementing agents, each with implementer evidence and a QA checklist
- **Assignee**: Claude Opus 5 (orchestrating) with subagents, 2026-09-17
- **Source**: `docs/AGENT-FEATURES-RESEARCH.md` recommendations and `issues/feature_intake.txt` (2026-09-17): "how to open files by typing their names, maybe @? any file that can be previewed in a pane will then start showing up with a type text filter."; "command suggestion (see how warp / claude do this)"; "add claude style recaps"; "allow queueing of terminal commands and agent commands -- make them colored or show up differently in the queue eg a different icon."; "dont hide the prompt box when any program is running, eg sudo apt upgrade, so i can queue agent / terminal commands. i would say, only hide the prompt box for full screen apps and for password input."

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
