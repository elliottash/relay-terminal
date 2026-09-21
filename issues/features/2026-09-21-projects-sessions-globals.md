---
id: P7SJ
type: work
status: discussing
labels: [feature, projects, sessions, switchboard]
assignee: codex
rank: m
created: '2026-09-21'
source: 'Codex conversation, 2026-09-21'
links: {plans: [], commits: [], evidence: [], related: [916B, TVE1, Y2MP], github: null}
---
# One pane for Projects, Sessions, and Globals

## Issue
Owner, initial request:

> rather than listing projects in agent options
>
> [Image #1]
>
> add them as a tab in the sessions manager
>
> actually, how about we change that to projects and sessions and move it to  ctrl shift p? or is a separate projects pane valuable? i think now we need a separate projects pane, ctrl shift p. how similar is this to the switchboard HQ idea i have a card about. analyze what we have and give me options

Owner, research request:

> before we decide this, can you research what warp, vscode, etc other systems / harnesses do for something like this to see what we can learn

Owner, after comparing separate panes, one pane with views, and a persistent sidebar:

> i am liking the middle option. can you include a "globals" tab that has our proposed "swithcboard hq" functionality

> and ctrl shift g can open globals

> i dont need the pane screenshot key, not sure why i have that

## Decisions
- Owner: "i am liking the middle option. can you include a \"globals\" tab that has our proposed \"swithcboard hq\" functionality" — develop the shared-pane option with three tabs: Projects, Sessions, Globals.
- Owner: "and ctrl shift g can open globals" — Ctrl+Shift+G selects Globals.
- Owner: "i dont need the pane screenshot key, not sure why i have that" — remove the default Ctrl+Shift+G binding from pane screenshot capture; no replacement screenshot shortcut needed.

## Discussion points
The conversation is still design work; no UI implementation has been requested in this stage.
The Globals tab is the home for the proposed HQ functionality in card #Y2MP. That card's
remaining memory/runtime and scope decisions stay open; choosing the tab does not settle them.

## Planning notes
Proposed interaction:
- Ctrl+Shift+P opens the shared pane on Projects; Ctrl+Shift+Y opens Sessions;
  Ctrl+Shift+G opens Globals. If the pane is already open, select and focus the requested tab
  rather than create another instance. Preserve each tab's search and selection.
- Projects replaces the known-project management list in Options. Show pinned/recent projects,
  active sessions, and explicit actions for opening a project, attaching this tab, opening its
  Switchboard, and starting a session. Put discovery provenance in details and Forget in a
  secondary action. Browsing a project must not silently attach the current tab.
- Sessions keeps the existing conversation/terminal-history search and project filters, including
  work outside projects. Reuse the same session data for active-session rows in Projects.
- Globals hosts HQ's global memories and aliases, with global instructions visible through their
  existing source files. Provide search, creation/editing of supported global records, and clear
  scope/source information; show when project-specific content overrides a global entry.
  Keep project memories and work on their project's Switchboard. Global instruction files can be
  inspected/edited in place without migrating them into the HQ store.
- A Globals view must be backed by the memory loading and global-board work in card #Y2MP;
  an empty placeholder or a list of unused memory records does not deliver HQ functionality.
- Recommended boundary: Globals does not silently become a default inbox for loose work cards;
  retain card #916B's default-project/picker routing unless the owner changes that decision.
- The default keymap currently gives Ctrl+Shift+G to `agent.screenshotPane`; remove that default
  when adding Globals, keeping the action available in Actions. Ctrl+Shift+P currently belongs
  to Actions in the Warp and VS Code presets: resolve that preset collision explicitly during
  shortcut design. Add shortcut hints using live bindings.

Current reuse points: `src/Conversations.{h,cpp}` (`SessionManager`, tab support),
`src/ProjectPicker.{h,cpp}`, `src/Projects.{h,cpp}` (registry), `src/RelayWindow.h`
(pane hosting and Options project rows), and `src/Keymap.h`. Card #TVE1 tracks consistent
project identity for grouping. These are findings, not a finalized execution plan.

Research behind the proposal (official documentation reviewed 2026-09-21):
- [VS Code session management](https://code.visualstudio.com/docs/agents/run/sessions/manage-sessions): shared sessions across workspace-scoped Chat and the cross-workspace Agents window; the latter is Preview.
- [Zed parallel agents](https://zed.dev/docs/ai/parallel-agents): terminal and agent threads grouped beneath projects, with a separate history view.
- [Warp Drive](https://docs.warp.dev/knowledge-and-collaboration/warp-drive/ai-objects): reusable knowledge and global/project rules provide the closest HQ comparison.
- [Warp Tab Configs](https://docs.warp.dev/terminal/windows/tab-configs/): launching a saved working setup is distinct from resuming a conversation.
- [Claude Code sessions](https://code.claude.com/docs/en/sessions): searchable history can widen from the current repository to all projects.

