---
id: 2Y96
type: work
status: planned
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

## Plan
Stages 6a/6b keep their names from the #JN7X staging. The cost guard is already shipped and stays closed at the end of this plan.

**Goal.** A pane's agent sandbox is its tab's project when the tab is attached, else the directory the pane was opened in — never the launch directory. Acceptance: a pane opened in `~/Downloads` cannot write into the launch project; an attached tab's new conversations use the project; the conversation list does not scatter.

**Findings (what the code does today).**

- `Pane::m_workspace` is frozen in the constructor (`src/Pane.h:473`) and feeds: the `configure` messages (`withSessionFields`, `src/Pane.h:4009`; startup `src/Pane.h:11562`, settings dialog `src/Pane.h:14717`), instructions scan (`src/Pane.h:2518`), `.relay/exports` (`src/Pane.h:3040`), aliases (`src/Pane.h:5241–5481`), terminal history (`src/Pane.h:6997`), the conversations listing (`src/Pane.h:10917`), and relative-path resolution (`src/Pane.h:4654, 4990, 5083, 9290`). Only `setAgentWorkspace` (`src/Pane.h:2257`, #D60R) ever changes it.
- Every new pane inherits the launch workspace: `RelayWindow::paneNode(cwd)` puts the active pane's workspace — else `m_manager->workspace()` — into the node (`src/RelayWindow.h:456`); `WindowManager::newWindowAt` passes `m_workspace` outright (`src/WindowManagerImpl.h:235`); `createPane` resolves a missing `workspace` to the manager's launch directory (`src/RelayWindow.h:4893–4897`); `activeCwd()` falls back to it (`src/RelayWindow.h:498`); four companion-terminal fallbacks use it (`src/RelayWindow.h:1312, 3821, 3897, 5971`); the empty-workspace guard uses the process cwd (`src/Pane.h:11549`). Board Execute/Verify panes are already correct (`src/RelayWindow.h:4752, 4774`), as is the split-pane copy (`:766`).
- The worker already takes `session_dir` on `configure` (`backend/relay_core/session_protocol.py:152`, `agent_options`), defaulting to `$XDG_DATA_HOME/relay/sessions/<sha16(normalized workspace)>` (`backend/relay_core/sessions.py:46`). The GUI never sends it. `projects::keyFor()` equals that digest (`src/Projects.h`, pinned by `tests/projects_test.cpp` + `tests/test_conv_index.py`), so the GUI can compute the bucket itself. A store under `sessions/loose` is still under `sessions_root()`, so it is indexed (`backend/relay_core/sessions.py`, `SessionStore.index`).
- The attach model exists: tabs unattached by default, `attachTab`/`detachTab` (`src/RelayWindow.h:4314/4334`), `tabProject` (`:4412`), panes told via `set_board` (`Pane::setBoard`, `src/Pane.h:12497`; the loop at `src/RelayWindow.h:4590`), reasons in `src/Projects.h` — `kReasonAgentWrite` ("agent-write") is defined and labelled (`src/ProjectPicker.cpp:65`) but nothing triggers it yet. `candidateFor()` maps a cwd to a project, filesystem-only.
- Cross-bucket resume already works: the resume message carries `session_dir` (`src/Pane.h:6864`), `Agent.resume` builds a store for it (`backend/relay_core/agent.py:2489–2492`), and the Sessions pane matches panes by `sessionDir()` (`src/RelayWindow.h:3926`).
- Plain `relay` with no restore-blocking flag only restores (`src/main.cpp`, `startFresh`); when `restoreSavedLayout()` reopens windows, nothing opens in the current directory.

**Steps.**

1. **6a — new panes take the opening cwd.** `paneNode(cwd)` sets `workspace` to that same `cwd`; `createPane` resolves a missing `workspace` from the spec's `cwd`, and only a spec with neither falls back — to `$HOME`, not the manager workspace; `activeCwd()`'s no-pane fallback and the four companion-terminal sites (`1312/3821/3897/5971`) use the tab's attached project if any, else `$HOME`; `newWindowAt(cwd)` passes `cwd`; `src/Pane.h:11549` falls back to `$HOME`. Grep for remaining `m_manager->workspace()` readers (`src/RelayWindow.h:1229, 5138, 5144`) and give each a real answer. Restored layouts keep each pane's recorded workspace (`restorableTabs`, `src/RelayWindow.h:468`) — pre-change layouts reopen old panes with the old value; leave them (see Risks).
2. **Session buckets, both halves in one commit.** Add a GUI helper beside `projects::stateDirectory()` for `$XDG_DATA_HOME/relay/sessions`, and `Pane::onSessionDir` (a window-filled callback like `onBoardSettings`, `src/Pane.h:619`): attached tab → `sessions/<keyFor(project)>`, unattached → `sessions/loose`. `withSessionFields` adds it to every `configure`. Instructions, aliases, terminal history and the conversation listing then follow `m_workspace` unchanged.
3. **6b — an attached tab's new conversations use the project.** On attach/detach, `attachTab` tells each pane a *pending* workspace (`Pane::setPendingWorkspace`): the next `configure` sends `workspace = project` and flips `m_workspace`; a mid-conversation attach changes only the board (`set_board`, protocol 19.11) and never the running sandbox. Panes created in an already-attached tab take `tabProject(page)` as workspace in `createPane`.
4. **Re-file on attach (owner, 2026-09-18), without restarting.** New worker message `session_refile {session_dir}`: the worker — the one writer — moves its live session's `<id>.json/.meta.json/.blobs/.threads` into the bucket, re-points `Agent.store`, refreshes the index row (`ConversationIndex.update_session(data, new_dir)`), and answers with the new `session_dir` so the pane updates `m_sessionDir` (`src/Pane.h:4061`). Sent from the `pane->setBoard` loop on attach only; detach re-files nothing. Document in `docs/AGENT-SESSIONS-PROTOCOL.md` (§1 table + a §19 subsection).
5. **Agent writes attach an unattached tab (`agent-write`).** The pane already renders the turn's file writes; add `Pane::onAgentWrote(paths)` → the window maps each path with `candidateFor()`. A write inside a *known* project (`Registry::isKnown`) on an unattached tab → `attachTab(page, project, kReasonAgentWrite)`. Attached to A while writes land in known B ≠ A → one hint, id `project.elsewhere.<keyA>.<keyB>`, limit 1 (once per pair), offering "open B in a new tab" (the existing open-directory pane, `src/RelayWindow.h:745`); moves nothing. Register the hint per the WARP.md standing rule.
6. **Plain `relay` in another repo.** In `src/main.cpp`, when `!startFresh` and windows were restored, also open one unattached tab in the launch directory via the first restored window (`addTab(paneNode(path))`), skipped when a restored tab already has that cwd; keep `newWindowAt` for the nothing-restored path (fixed by step 1).

- [x] **Cost guard on recursive walks** — shipped 2026-09-19, `tools.walk_cost_refusal`, shared with the board's `search_files`. No further work.

**Risks.**

- *Old layouts* reopen pre-change panes with the launch workspace recorded in them. Recommendation: leave them — a silent remap on restore rewrites a user-visible sandbox — but say so in the restore note if a remapped-looking pane is detected. **Owner question:** accept that, or remap on restore when the recorded workspace equals the launch directory and the pane's cwd does not?
- *Re-file race:* a background subagent autosave during the move can recreate the file in the old bucket. Run the move under the worker's exclusive lock (`TurnSupervisor.run_exclusive`, as `compact`/`set_model` do) and re-check the old directory after re-pointing.
- *Wide sandboxes:* a pane in `$HOME` gets `$HOME` as sandbox — accepted (owner, 2026-09-19); the cost guard bounds the crawl, not the access.
- *The `loose` bucket* mixes every unattached conversation by design; each pane's list still scopes to its own workspace, and everything is reachable through "All projects" and the project filter (#916B).
- *Hint flood:* the pair-keyed id with limit 1 is the whole defence; keep the text to one line.

**Verify.**

- Build with `scripts/relay-build`; run `./scripts/test.sh` and `ctest --test-dir build`.
- Extend `tests/windowstate_test.cpp` (createPane/resolveDirectory fallbacks), `tests/projects_test.cpp` (sessions-root/keyFor helper, `loose`), `tests/test_session_protocol.py` (configure `session_dir`; the `session_refile` handler — files moved, index row's `session_dir`, reply event); hint ids in `tests/hints_test.cpp` if unit-testable.
- Live under Xvfb with an isolated `XDG_CONFIG_HOME`, launched from project A: a new tab after `cd ~/Downloads` shows Agent workspace `~/Downloads` (cwd chip tooltip) and a `write_file ./probe.txt` lands there, not in A; its conversation is saved under `sessions/loose` and the list scopes to `~/Downloads`; opening the Switchboard attaches the tab — the open conversation's files move to `sessions/<keyFor(A)>` without a reset, and the *next* conversation configures with A as workspace; agent writes in known project B on an unattached tab attach it with reason `agent-write`; attached to A, writes in B show the hint exactly once; `relay` started in project B restores A's windows and opens one new unattached tab in B; a pane in `$HOME` still gets the recursive-walk refusal.
