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
**2026-09-26 refresh (supersedes stale paths and test commands below).** The core defect is
still present: `src/Pane.h` keeps `m_workspace` from construction, `RelayWindow::paneNode` and
several companion-pane paths still read `m_manager->workspace()`, and `src/main.cpp` restores
windows without opening the invocation directory when restoration succeeds. The cost guard is
already shipped. The attached-project and session-bucket design remains useful, but execute it
against today's split `Pane*`/`RelayWindow*` sources and the current guest workspace integration.
Use `rg` to locate each call site rather than the 2026-09-19 line numbers below.

**Updated sequence.** First fix new-pane/new-window workspace selection and pin it with
`windowstate`/workspace tests. Then make new conversations use the attached project's key and
unattached conversations use a stable loose bucket; verify indexing before any migration. Next
implement re-filing of an open conversation on attach under an exclusive worker operation with
resume tests. Last, wire agent-write auto-attach, the cross-project hint, and plain `relay` restore
plus new-directory tab. Keep the owner's existing decision that `$HOME` remains a valid wide
sandbox with the recursive-walk cost guard.

**Decision still needed.** Old saved layouts can contain the launch directory as their recorded
workspace even when the pane's cwd differs. Recommend preserving that recorded sandbox on
restore and correcting only newly created panes; silent remapping would change the authority of
an existing agent. The owner has not answered this question on the card.

**Verification update.** Use targeted `windowstate`, `projects`, session-protocol and hint tests,
then an isolated live run for the attach/refile and restore scenarios. The old blanket
`./scripts/test.sh` and full `ctest` instruction below conflicts with current project guidance.

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

- Build and run the targeted `windowstate`, `projects`, session-protocol and hint tests through
  the project's current isolated verification workflow; do not run the full suites by default.
- Extend `tests/windowstate_test.cpp` (createPane/resolveDirectory fallbacks), `tests/projects_test.cpp` (sessions-root/keyFor helper, `loose`), `tests/test_session_protocol.py` (configure `session_dir`; the `session_refile` handler — files moved, index row's `session_dir`, reply event); hint ids in `tests/hints_test.cpp` if unit-testable.
- Live under Xvfb with an isolated `XDG_CONFIG_HOME`, launched from project A: a new tab after `cd ~/Downloads` shows Agent workspace `~/Downloads` (cwd chip tooltip) and a `write_file ./probe.txt` lands there, not in A; its conversation is saved under `sessions/loose` and the list scopes to `~/Downloads`; opening the Switchboard attaches the tab — the open conversation's files move to `sessions/<keyFor(A)>` without a reset, and the *next* conversation configures with A as workspace; agent writes in known project B on an unattached tab attach it with reason `agent-write`; attached to A, writes in B show the hint exactly once; `relay` started in project B restores A's windows and opens one new unattached tab in B; a pane in `$HOME` still gets the recursive-walk refusal.

## Freshness check against the rest of the board (2026-09-19)
**Verdict: fresh, nothing to merge.** No card and no design doc proposes a per-pane agent
workspace. `docs/SWITCHBOARD-DESIGN.md` (§4.1) and `docs/PROJECT-INIT-AND-IMPORT.md` only ever
discuss the *board's* project, never the agent's sandbox, and #JN7X closed by putting the attach
model and the pane's own workspace deliberately out of scope. What exists is neighbours whose edges
should be named on this card.

- **#JN7X** (needs-qa-llm) — fixed the *board's* project and left "the owner's attach model … and
the agent's own workspace" to later work. This card is its stage 6. No overlap.
- **#916B** (needs-qa-llm) — picker, chip, known projects, the Sessions Project chooser (protocol
  14.3), and `defaultProject()` after the personal inbox was dropped. Two edges. (a) Its "loose
  cards" are *board* filing; this card's `loose` is a *session* bucket — same word, different
  thing, so say it once, and do not revive `projects::inbox()`. (b) 14.3 already filters the
  Sessions list by project, so "the conversation list does not scatter" here means the *rows are
  filed under the right workspace*, not that the filter is built. Put that in the acceptance line.
- **#TVE1** (ready) — groups tabs and conversations by `keyFor`; this card's buckets *are*
  `keyFor(project)`, and TVE1 asks for "one place both read the key from". The bucket helper in step
  2 must be that one place. Acceptance split: rows filed right here, how they are grouped there.
- **#64KE** (needs-qa-llm) — restore already saves each pane's `cwd` and `workspace` and its
  `resolveDirectory()` falls back cwd → workspace → window workspace → `$HOME`, the same function
  family step 1 changes and that card pinned in `tests/windowstate_test.cpp`. Its documented
  **known gap 6** is exactly what step 6 removes: "`--workspace` implies `--fresh`. Passing a
  workspace explicitly always opens one new window; there is no way to say 'restore *and* also open
  this directory'." **Recommendation:** leave `--workspace` meaning `--fresh` (unchanged) and let
  only plain `relay` gain the new tab, then correct that gap in #64KE — otherwise the two cards
  contradict each other once this lands.
- **#0STR** (needs-qa-llm) — the header tooltip now reads "The terminal is in …, and the agent's
  workspace is …". That is the visible surface for this bug and the cheapest way to check step 6a
  by eye; the Verify section already uses it.
- **#G8DK** (aliases, needs-qa-llm) — a local alias resolves from the *workspace*
  (`<repo>/issues/aliases/`, else `<repo>/.relay/aliases/`), so after 6a an unattached pane in
  `~/Downloads` looks in `~/Downloads`. Right in principle, but "What the workspace feeds" lists
  alias scope without saying what changes; one line in Verify.
- **#E99H / #3KB7** (needs-qa-llm / in-progress) — E99H made absolute paths and `..` allowed
  *inside* the workspace; 3KB7 states the confinement as "one directory, not a list" and offers
  "folders the agent may read outside the workspace". This card changes which directory that is:
  3KB7's statement stays true, but its item 5 is mostly redundant for a pane already standing in
  `$HOME`, and its wording should be re-read when this lands. The ssh path (#S5SH) already confines
  to "the directory their shell is in", so this makes local and remote agree rather than diverge.
- **The docs already claim the end state.** `docs/ARCHITECTURE.md:1885` ("A pane's workspace is the
  directory the pane is in, so a pane standing in `$HOME` or `/` gets a very wide sandbox") and
  `docs/AGENT-SESSIONS-PROTOCOL.md:183` / `:2291` (the cost guard) describe post-6a behaviour as
  fact, while `docs/ARCHITECTURE.md:1448` and `docs/SWITCHBOARD-DESIGN.md:152` still give "frozen at
  creation / inherited from the launch directory" as the reason the board does not consult
  `workspace()`. The docs contradict each other today; this card should carry the doc pass, and the
  cost-guard sentences become true only when 6a lands.

**To fold in (small, not new work):** the `--workspace` answer in #64KE; one sentence separating
`loose` (sessions) from "loose cards" (#916B); the shared `keyFor` helper and the acceptance split
with #TVE1; the doc pass on `ARCHITECTURE.md:1448` and `SWITCHBOARD-DESIGN.md:152`; and alias scope
in Verify.
