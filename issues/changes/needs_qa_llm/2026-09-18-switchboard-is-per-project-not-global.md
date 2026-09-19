---
id: JN7X
type: work
status: needs-qa-llm
labels: [bug]
component: [gui, worker]
milestone: beta
workstream: switchboard
assignee: agent
implemented_by: Claude Fable 5.1 with Claude Opus 5 subagents (Claude Code session), 2026-09-18
rank: '05'
created: '2026-09-18'
acceptance: a non-Claude model QA session runs the checklist under Xvfb with an isolated profile and records it under `docs/qa_evidence/2026-09-18-switchboard-per-project/`
source: 'owner, 2026-09-18: "i realized an important and complex bug with the siwtchboard. it seems to be global, and not tied to a project. if i move out of the repo its still the same one."'
links: {plans: [], commits: [384fac4, 49167a1], evidence: ['docs/qa_evidence/2026-09-18-switchboard-per-project/'], related: [23XM], github: null}
---
# The Switchboard is the board of the project a pane is in, not one global board

## Issue

Wherever a pane stood, Ctrl+Shift+S opened the board of the directory Relay had been launched from.

- `RelayWindow::boardWorkspace()` asked the pane's *agent workspace* first — fixed when the pane is
  created and inherited from the launch directory by every new pane and window — and then fell back
  to `m_manager->workspace()` and `QDir::currentPath()`, which are that same launch directory.
- The worker resolved an empty `workspace` to the relative path `issues`, i.e. the launch
  directory's board (186 cards of this repository, from anywhere).
- One Switchboard worker per window was re-pointed by whichever board opened last and broadcast its
  events to every board view in every tab: project A's view was reset to B's cards, and drags,
  quick-adds and undos from A's pane then wrote into B's `issues/`. Restoring a layout with two
  projects' boards was enough to reach it.
- The GUI walked up to find `issues/board.yaml`; the worker did not, so `/card` and the `#` picker
  failed in a subdirectory where Ctrl+Shift+S worked.

## What landed

- `src/BoardWorkspace.cpp` `relay::boardRootFor()`: nearest ancestor with `issues/board.yaml` of the
  first absolute candidate that has one. `boardWorkspace()` passes only the active pane's terminal
  directory, then its agent workspace. No window-wide or process-wide fallback.
- One worker per board root per window; each reaches only its root's views; stopped with its last
  view and on window close (the single worker was never asked to exit). `BoardView` drops events
  carrying another board's `root`.
- A tab's existing board says so when it is not the active pane's project; a card that is not on
  the board says so after the retries; a restored board whose project lost its board reopens as a
  terminal in that directory.
- Worker: a board only from a named workspace or `board.dir`; the same walk-up as the GUI, with the
  project root as the board's repo; `root` on every `board_*` event (protocol 19.1); re-pointing
  forgets the previous board's card conversation and row snapshot.

Not in this card: the owner's attach model (tabs unattached by default, explicit project actions
attach, boards kept in Relay's data directory unless moved into the repo). That is separate work
built on this.

## Implementer evidence (not a QA verdict)

`docs/qa_evidence/2026-09-18-switchboard-per-project/` — four screenshots and `steps.txt` from an
Xvfb run with Relay launched from project A: A's board; after `cd` to B the key focuses the tab's
board and names both projects; a second tab opens B's board while two workers run; tab 1 still
shows A's cards; closing boards and the window leaves no worker. Each commit's exact tree was
exported, configured, built and passed `ctest` (49 and 50 tests, `backend-and-bash` included).

## QA checklist

1. Launch Relay from a project with a board, `cd` to a directory with no board above it, press
   Ctrl+Shift+S in a fresh tab: the status bar names that directory and no board opens.
2. Two projects with boards, one tab each, same window: each Switchboard shows its own cards; drag a
   card in one and confirm only that project's `issues/` changed (`git status` in both).
3. With both open, quick-add a card in each: `ps` shows two `worker.py` with
   `RELAY_PANE_ID=switchboard`; close one board, one remains; close the window, none remain.
4. In a tab whose board is project A, `cd` a pane into project B and press the key: A's board is
   focused and the status bar says the pane belongs to B.
5. From `<project>/src`, `/card test` and the `#` picker work and the card lands in the project's
   `issues/`; `.relay/board-rate.json` is in the project root, not in `src/`.
6. Save a layout with two projects' boards, quit, relaunch from a third directory: both boards
   return with their own cards.
7. Click a `#ID` from another project's output: after a few seconds "No card #ID on this board."
8. `tests/test_board_protocol.py` `PerProjectTests` and `WorkerWorkspaceTests`, `boardworkspace` and
   `board` in `ctest` pass.
9. Independence: implemented by Claude models; QA must come from a different model family.
