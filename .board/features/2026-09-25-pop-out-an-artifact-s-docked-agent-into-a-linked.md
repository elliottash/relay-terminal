---
id: 2FQ9
type: work
status: planned
labels: [feature, panes, agent-ui, switchboard]
rank: zzzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
source: 'Owner in a Relay pane, 2026-09-25; slice of #P2W8 (U3)'
links: {plans: [], commits: [], evidence: [], related: [P2W8, E85D, CTRN, Y2BA], github: null}
---
# Pop out an artifact's docked agent into a linked shell pane, and dock it back

## Issue
further, you can pop out the artifact agent and it goes into a side-by-side separate connected shell pane. so that agent is working on the artifact but has access to the shell as well.

## Plan
Slice 5 of #P2W8 (decision U3, mechanism c). **Wave 2: waits for the hold on #E85D to lift**, because the link between the two panes is an `ArtifactWorkspace` group (`src/ArtifactWorkspace.h`, untracked in the tree).

**Goal.** From a card page (later any artifact pane), one action moves its docked agent into a leaf beside it with a shell; the same agent, same conversation (`<tab>/card:<ID>`), same tab worker, thread provenance intact; one action docks it back.

**Findings.** A docked console is a no-shell `Pane` (`RelayWindow::createAgentConsole`, `src/RelayWindow.h:5794`) that shares the tab's `BoardWorker`; `sharesWorker()` (`src/Pane.h:10348`) is what stops it starting a pty. The agent's own shell commands run in the worker's executor, not in the pty, so shell access for the agent and a pty for the human are separable. Run (`startBoardTask`) is a different thing: a fresh agent on the brief in a new shell pane; it stays.

**Steps.**
1. `Pane`: split "starts its own worker" from "starts a pty"; a console pane may `startTerminal()` while `sharesWorker()`; typed shell lines go to its pty, agent lines still go through `sendFromConsole` with `surface: card:<ID>`.
2. `RelayWindow::popOutConsole(ConsoleHandle)`: reparent the console widget out of the host frame into the splitter beside the host (`dockBeside`), start the shell in it, header "#K7Q2 · agent ⤴" with the link glyph; the host draws a one-line placeholder "Agent is in the pane beside this one · Dock back". `dockConsole(...)` reverses (stops the pty, reparents back).
3. Group: register host and popped-out pane as an `ArtifactWorkspace` group (roles `artifact`, `console`) so restore rebuilds both and closing the host asks about the console; until restore support lands, a restart docks the console back.
4. Entry points: a button in the card page's action row ("Open a shell for this agent") and a palette action; `Ctrl+Shift+E` proposed, unbound by default.
5. Docs: `docs/ARCHITECTURE.md` "Agents are consoles" gets the pop-out paragraph; protocol unchanged.

**Files.** `src/Pane.h` (`sharesWorker`, `startTerminal` guard), `src/RelayWindow.h` (`createAgentConsole`, new `popOutConsole`/`dockConsole`), `src/BoardPane.cpp` (the action), `src/ArtifactWorkspace.*` (after the hold), tests `tests/consolemode_test.cpp`.

**Verify.** Live: a card agent mid-conversation is popped out, runs `git status` in the pty, answers a follow-up that references the earlier turn, is docked back with the transcript intact; the thread on disk shows both turns with `turn=` provenance.

## Tasks

- [ ] Pane may start a pty while sharing the tab worker <!-- t:w0 -->
- [ ] popOutConsole / dockConsole with placeholder and header link <!-- t:mx blocked_by=w0 -->
- [ ] Workspace group registration and restore (after the #E85D hold lifts) <!-- t:rp blocked_by=mx -->
- [ ] Entry points, docs, live evidence <!-- t:k3 blocked_by=mx -->
