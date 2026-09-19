---
id: 2Y96
type: work
status: ready
labels: [feature]
component: [gui, worker]
milestone: beta
workstream: switchboard
rank: '3'
created: '2026-09-19'
acceptance: a pane opened in ~/Downloads cannot write into the launch project; an attached tab's new conversations use the project; the conversation list does not scatter
source: '#JN7X investigation, 2026-09-18: `Pane::m_workspace` is set at construction and inherited from the launch directory by every new pane and window (`src/WindowManagerImpl.h` newWindowAt, `RelayWindow::paneNode`), the same class of bug as the global Switchboard; owner agreed the tab''s conversations are re-filed under its project on attach ("lets try it that way")'
links: {plans: [], commits: [], evidence: [], related: [JN7X], github: null}
---
# The agent's file sandbox is the tab's project, or the directory the pane is in, and never the directory Relay was launched from

## Issue

Every new pane and window inherits the agent workspace of the directory Relay was launched from.
A pane standing in `~/Downloads` therefore has the Relay checkout as its file sandbox, its project
instructions and its conversation bucket. #JN7X fixed this for the Switchboard only.

## What the workspace feeds (so nothing is missed)

Relative tool paths, `session_dir` (`sessions.default_session_dir`), the conversation index's
project filter, project instructions discovery, `plans_dir`, terminal history, alias scope,
`.relay/exports`, and every relative path the pane prints.

## Plan (from the #JN7X staging, stage 6)

- [ ] 6a. New panes, tabs and windows take the opening pane's **cwd** instead of
      `m_manager->workspace()`. Pass `session_dir` explicitly: attached panes use the project's
      key, unattached panes share one `loose` bucket — both halves in one commit, or the history
      looks lost.
- [ ] 6b. An attached tab's **new** conversations use the project as the workspace. It can only
      apply at conversation start: `configure` starts a new conversation, so a mid-conversation
      attach changes the board (protocol 19.11) and never the sandbox.
- [ ] On attach, **re-file** the tab's open conversations under the project's key (owner, agreed
      2026-09-18), without restarting them.
- [ ] Agent **writes inside a known project attach** an unattached tab (reason `agent-write`);
      work in project B while attached to A shows one hint offering B in a new tab, once per pair,
      and moves nothing.
- [ ] Plain `relay` typed in another repo restores the previous windows **and** opens a new
      unattached tab in the current directory (`src/main.cpp`, `startFresh`); today it only
      restores, which is the second reason the old project seemed to follow the owner around.

- [x] A **cost guard** on recursive walks, so the wide sandbox a pane in `$HOME` gets is not a wide
      crawl: `run_command` refuses a recursive search or listing rooted at the home directory, `/`
      or a directory the home sits under, naming a narrower path to pass. Shipped 2026-09-19 —
      `tools.walk_cost_refusal`, shared with the board's `search_files`.

## Open for the owner — answered

A pane in `$HOME` or `/` gets a very wide sandbox under 6a. Cap it (refuse file tools above some
depth until a folder is chosen), or accept it?

**Owner, 2026-09-19: accept it, and add a cost guard rather than a permission guard.** The agent
acts here without per-action approvals by design, so refusing file tools by depth would be theatre —
a pane in `$HOME` may read and write in `$HOME`. What is refused is the **cost** of crawling it: a
recursive search or listing (`grep -r`, `ls -R`, `find`, `rg`, `ag`, `ack`, `fd`, `tree`, `du`)
whose effective root is the home directory, the filesystem root, or a directory above the home
(`/home`) comes back with an error that says so and gives the shape of a narrower path, which the
model can act on without asking. A non-recursive `list_directory` of those directories stays
allowed, an explicit path below them always runs, and the existing entry, byte and time ceilings are
unchanged. The board's card-turn `search_files` shares the guard. Like the command denylist it is
honoured, not unevadable (`backend/relay_core/tools.py`, "the recursive-walk cost guard";
`docs/ARCHITECTURE.md` › Tools; protocol section 11, "Too wide to crawl").
