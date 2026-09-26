---
id: 2FQ9
type: work
status: needs-verification
labels: [feature, panes, agent-ui, switchboard]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude:ashe-ethz-ch
session: dd00400e-62fe-4527-93d1-4fc33b778c3c
rank: zzzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
source: 'Owner in a Relay pane, 2026-09-25; slice of #P2W8 (U3)'
links: {plans: [], commits: [aa776aa14b7e], evidence: [docs/qa_evidence/2026-09-26-2fq9-linked-agent-shell/], related: [P2W8, E85D, CTRN, Y2BA], github: null}
---
# Pop out an artifact's docked agent into a linked shell pane, and dock it back

## Issue
further, you can pop out the artifact agent and it goes into a side-by-side separate connected shell pane. so that agent is working on the artifact but has access to the shell as well.

## Plan
**Goal.** Move a card or file artifact's existing docked agent into a linked shell leaf beside its host, then dock that same agent back. Keep its conversation, artifact routing and card-thread provenance through both moves.

**Findings.** `RelayWindow::createAgentConsole` (`src/RelayWindow.h`) creates a `Pane` hosted inside the artifact and attached to the tab worker. `Pane::startTerminal` (`src/PaneRuntime.cpp`) starts the pty only when `hasShell()`; `Pane::submit` first offers input to its artifact context, so a popped console needs explicit shell-input routing. Card consoles are created in `src/BoardPane.cpp`; file consoles are created lazily by `ArtifactDock::ensureConsole` in `src/FilePanes.cpp`, whose head row has no pop-out button. `ArtifactWorkspace` (`src/ArtifactWorkspace.h`) and its window integration (`src/RelayWindowWorkspace.cpp`) have landed since the earlier plan; its roles are `Editor` and `Console`, with saved leaf membership. #Y2BA also added a distinct card-pane kind and an existing "open card in its own pane" action, which must stay distinct from moving the agent.

**Steps.**
1. In `src/Pane.h` and `src/PaneRuntime.cpp`, separate pty presence from worker ownership. A shared-worker console can start and stop its pty without starting another worker; explicit shell input goes to the pty, while agent input retains `Context::submit` and its card/file surface. Keep existing docked consoles shell-free.
2. In `src/RelayWindow.h`, add pop-out/dock operations for the existing `ConsoleHandle`/`Pane`. Move the widget from its host layout into a leaf beside that host with `dockBeside`; retain the context wrapper and tab attachment. Show linked-agent chrome and a one-line host placeholder with "Dock back"; reverse the move, stop the pty, and restore the original layout and focus. Handle a closed host or console without dangling pointers.
3. Register the host leaf and shell leaf in the landed `ArtifactWorkspace` model as editor/console members. Extend layout save/restore for this linked state and define close behavior: closing the host offers to keep or close the agent; after restart, recover the linked pair when both leaves restore, otherwise dock the agent safely. Check existing group membership before joining a host that belongs to a larger workspace.
4. Add the pop-out/dock control to the card page action row (`src/CardPane.h`/`src/BoardPane.cpp`) and the file artifact agent head row (`ArtifactDock::ArtifactDock`, `src/FilePanes.cpp`), plus a palette action. Keep the existing card-page "open in its own pane" action clearly separate. Leave `Ctrl+Shift+E` unbound unless keymap review finds it free and useful.
5. Update `docs/ARCHITECTURE.md` ("Agents are consoles" and workspace layout) and focused tests for worker identity, input routing, move/dock, close and restore.

**Risks.** Reparenting an active `Pane` can disturb focus, destruction order or tab lookup; preserve its context owner and wrapper lifetime. The landed workspace model currently treats members as leaf IDs, so a hosted console needs a distinct, stable saved member only while popped out. Avoid stealing the host's existing editor/console role in a larger group. No owner decision is needed for an unbound shortcut.

**Verify.** Read `docs/BUILDING.md`, build with `scripts/relay-build`, and run focused `consolemode`, artifact-workspace, card-pane and layout tests. In a live session, pop out a card agent mid-conversation, run `git status` in the shell, send a follow-up referring to an earlier turn, dock it back, and inspect the card thread for both `turn=` entries. Repeat from a file artifact head row; check host/console close choices and restart restoration.

## Tasks

- [x] Pane may start a pty while sharing the tab worker <!-- t:w0 -->
- [x] popOutConsole / dockConsole with placeholder and header link <!-- t:mx blocked_by=w0 -->
- [x] Workspace group registration and restore (after the #E85D hold lifts) <!-- t:rp blocked_by=mx -->
- [x] Entry points, docs, live evidence <!-- t:k3 blocked_by=mx -->

## Done means
A docked card or file artifact agent can be popped into a linked shell pane beside its artifact and docked back through visible controls.
The same agent conversation, tab worker, artifact context, and card-thread provenance survive both moves; the shell accepts human commands while agent prompts keep their artifact routing.
Closing or restoring the linked panes gives a coherent workspace without losing the conversation. Failure is a missing pop-out control, a fresh agent/thread, lost transcript, or a shell prompt routed into the agent.

## Execution Summary
Landed in `aa776aa1`.

- **Pane (w0).** `Pane::setLinkedShell(on, cwd)` (src/PaneLinkedShell.cpp) starts a pty on the backend that already draws the console's transcript, so the surface is not rebuilt and no second worker starts. While linked, `hasShell()` is true and asks carry the terminal snapshot. The mode chip returns, and a line aimed at the shell (a typed `!`, or Terminal mode) is dispatched locally to the pty. Every other line and chord still reaches `Context::submit` first, so a card's Enter / Ctrl+Enter / Ctrl+Shift+Enter keep their meaning and `board_ask` still writes the thread. Off stops the pty and keeps the transcript. Engine: `TerminalBackend::stopProgram()`, and `TerminalSession` may start a new program once the last has ended. A link made while the old shell is still exiting waits for it. The shell runs without the local holder, so no stray tmux session is left behind.
- **Window (mx).** `RelayWindow::popOutConsole` / `dockConsole` (src/RelayWindowLinkedAgent.cpp) work for any hosted console. A placeholder (Show · Dock back) holds the console's slot in its host, whether a nested box layout or a splitter. The console moves into a `ToolPane::Kind::LinkedAgent` leaf beside the host (src/LinkedAgent.h). Its bar names the card (`#F9A1`) or the file and holds Dock back. The host's size limits on the console are saved and put back.
- **Close and restore (rp).** Closing the linked leaf, typing `exit` in its shell, or pressing Dock back all dock the agent back. A running command is asked about first. Closing the host docks the agent and then closes both. A tab teardown deletes the console before the host's context is freed, and the leaf's destructor puts back a console it still holds. The host leaf saves `agent_linked: true`, and a restored layout links the agent again.
- **Entry points and docs (k3).** "⤴ Agent + shell" sits on the card title row, separate from "⤴ Own pane" (#Y2BA), and reads "⤵ Dock agent" while the agent is out. "⤴ Shell" / "⤵ Dock" sits in the file agent's head row beside fold. The palette action is `agent.linkShell`, with no default key; it is also in the agent-callable writing actions. `docs/ARCHITECTURE.md` ("Agents are consoles") documents all of this.

**Decisions, not gaps.** (1) The plan said closing the host would *offer* to keep the agent. It closes the agent instead: the host owns the context the console wraps, so the agent cannot outlive it. The conversation is the worker's persisted (tab, card/file) one and returns when the artifact reopens. (2) The linked pair is not registered as `ArtifactWorkspace` editor/console members. A card page is not a file workspace, and giving a file's linked agent the `console` role would take the role from a group's own console, which the plan's Risks warned against. Save and restore go through the host's `agent_linked` flag instead.

**Incident.** During the drive, `relay-drive` and `relay-open` reached the owner's own Relay through the inherited `RELAY_OPEN_SOCKET` before I noticed. The thread entry of 2026-09-26 04:32 lists the commands that reached it, including `agent.clearQueue` and `pane.equalize`. `drive.sh` now unsets the variable.

## Tests
- `ctest -R '^linkedshell$'` (new; `relay-consolemode-tests --linkedshell-only`, 4 cases): passed in the land verify slot on tip + this change. Covers the pty on the same surface and shared worker, `!` and Terminal mode reaching the pty and never the context, Enter still reaching the context, stop keeping the transcript, a second link restarting the session, `exit` asking to dock back, a host fold held while linked, a hidden unused transcript shown while linked, and a terminal pane never linking.
- `ctest -R '^relay-engine-tests$'` and `^appcommands$`: passed.
- `ctest -R '^consolemode$'`: 11 failures, identical on `main` without this change. Already filed as `.board/changes/2026-09-25-consolemode-suite-red-at-head-after-2m26-and-pbz4.md`.
- `ctest -R '^keymap$'`: 1 failure on `main` (Ctrl+J `folds.step` vs Ctrl+Shift+J `program.delegate`, from #XPEB's `a71ca0d5`); noted on #XPEB. This change binds no keys.
- Live drive under Xvfb (`docs/qa_evidence/2026-09-26-2fq9-linked-agent-shell/`, README has the shot list). Card: popped out mid-conversation, `!git status` in the linked shell, a follow-up while linked recalled the docked turn's marker and the git output, docked back, and a follow-up recalled all three markers. The card thread holds all three turns under one conversation id. File: ⤴ Shell in the head row, `!ls`, and `exit` docked back by itself. Also covered: the palette action, × on the linked pane docking back, a fresh card showing its shell, restart restoring the linked pair, closing the host closing both, and closing a tab with the agent out, all with no crash.
