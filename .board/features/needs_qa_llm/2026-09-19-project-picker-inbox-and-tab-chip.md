---
id: 916B
type: work
status: needs-qa-llm
labels: [feature]
component: [gui, worker]
milestone: beta
workstream: switchboard
rank: '2'
created: '2026-09-19'
acceptance: Ctrl+Shift+S in a pane with no candidate project offers known projects; an attached tab shows its project and detaches in one click; a card filed in a tab with no project attached uses the default project (once set) or the picker
source: 'owner, 2026-09-18, designing the project model after #JN7X: a tab has at most one project; "i use warp a lot, just screwing around in my downloads or admin folders"; agreed to a "move to project…" action on inbox cards ("lets try it that way"). Revised owner, 2026-09-19: dropped the personal inbox board (see Decisions)'
links: {plans: [], commits: [28f2c27, 7e3fb9f, 5ee7259, 8e02fca, 6c68a2a, 604e01c7], evidence: [docs/qa_evidence/2026-09-19-project-picker/README.md], related: [JN7X, TVE1], github: null}
---
# A project picker and a chip on the tab that says which project it is attached to

## Issue

Stage 4 of the project model (the earlier stages are on `main`: `6a103c4`, `bdd53c0`, `c1c5fad`, and the
init question). What is missing is everything a person *sees* about attachment, and somewhere a
card goes when there is no project.

## Decisions

Owner, 2026-09-18, first pass:

1. Ctrl+Shift+S in a pane with **no candidate project** (`~/Downloads`, an admin folder) opens a
   picker: the known projects (`relay::projects::Registry::knownProjects()`, most recently attached
   first). Picking one attaches the tab (reason `picker`). A pane, not an overlay.
2. ~~The inbox is the one board that lives in Relay's data directory
   (`relay::projects::inbox()`, `$XDG_DATA_HOME/relay/boards/inbox/switchboard`). It is created by
   its first card. Proposed, not yet confirmed by the owner: make that folder a small git repository
   Relay commits to after each write, since nothing else backs it up.~~
3. ~~A card in the inbox gets a **"Move to project…"** action. If the target project has no
   Switchboard yet, the move asks the one-time init question first.~~
4. An attached tab shows a **chip** with the project's name; one click detaches. An unattached tab
   shows nothing at all — no "not attached" state.
5. The known-projects list is **removable** (`Registry::forget`) and shows why each project became
   known (`Record::reason`), from the Options pane. Lesson from Warp's #11899, where the list only
   grows.
6. The tab list and the conversation list **group by project** from the start (Warp's most-asked
   missing feature, #9875).

**Owner, 2026-09-19: dropped decisions 2 and 3.** The personal inbox — a standing board in Relay's
own data directory that every loose card would land in — is gone, and so is the git-repo-backup
idea that rode along with it in decision 2 ("proposed, not yet confirmed"): once there is no inbox
folder, there is nothing left to back up. `relay::projects::inbox()`, `Board::Inbox` and
`boardsRoot()` are removed from the C++ model, not merely unused, along with their tests
(landed alongside the hidden-board-folder decision's C++ half, `5c329706d63f`, since both touched
`src/Projects.h`/`.cpp` the same day). In its place: one optional setting, **"Default project for
loose cards"** (`relay::projects::defaultProject()`, `board/default_project` in `QSettings`, empty
by default — a project folder, not a board, so it reads as unset if the project is moved or
deleted rather than writing a card somewhere that is no longer there). Filing a card in a tab with
no project attached now works like this:

* a default project is set → the picker offers it preselected (or, once the picker supports it,
  the card is filed there directly without asking);
* no default, but there are known projects → the picker, exactly as decision 1 already described,
  with no separate "inbox" row;
* no default and no known projects → the existing one-time "Initialize a project and create a
  Switchboard here?" question, on whichever project the card would have gone to.

Decision 3's "Move to project…" action is superseded, not merely dropped: with no inbox there is
no inbox card to move, since a loose card now goes straight to a real project's board. A "move
this card to another project" action for an *already-filed* card may still be wanted some day, but
that would be a fresh decision made on its own merits, not a revival of this one.

Decisions 1, 4, 5 and 6 stand unchanged and are still open GUI work — see Tasks.

## Tasks

- [x] the hidden-board-folder discovery order and the `defaultProject()` setting that replaces the inbox, in `src/Projects.h`/`.cpp` (`5c329706d63f`) <!-- t:hb -->
- [x] `src/ProjectPicker.*` as a pane, fuzzy-filtered with `relayFuzzyScore`, offscreen widget test — known projects only, no inbox row, `defaultProject()` preselected when set, and "Initialize new project here" on top (`board_init {git_init: true}`, protocol 19.12) (`28f2c27`) <!-- t:2g -->
- [x] the tab chip and one-click detach; the palette's "Detach this tab from …" stays as the keyboard path (the chip teaches it) <!-- t:mn -->
- [x] known projects in Options: list, reason, forget; declined projects: list, undo; alongside them, the "Hidden Switchboard folder" toggle and the "Default project for loose cards" row (both already have their `QSettings` keys and reader/writer functions — `board/hidden_folder`, `board/default_project` — only the Options rows are missing) <!-- t:d2 -->
- [x] the board's "Hide this board's folder" / "Show this board's folder" action in the Switchboard's gear page (`board_folder`, protocol 19.17), its result on the notice line <!-- t:hf -->
- [x] Sessions by project: a Project chooser beside the Kind filter, asked of the worker as `project` / `outside_projects` (protocol 14.3) <!-- t:sf -->
- [ ] group tabs and conversations by `relay::projects::keyFor` — split out to #TVE1 (owner's decision of 2026-09-19: deferred) <!-- t:mg -->
- [x] shortcut hints for whatever gains a key (WARP.md standing rule): the picker has no key of its own, so the palette entry teaches Ctrl+Shift+S, and the chip teaches the palette's detach <!-- t:y9 -->

## What "done" means

In a tab with no project attached and a pane standing in no project (`~/Downloads`), Ctrl+Shift+S,
`/card <text>` and the palette's "Attach this tab to a project…" open the **project picker** as a
pane beside the one that asked: the projects Relay knows (most recently attached first, fuzzy
filtered as you type), the "Default project for loose cards" preselected when set, and
**"Initialize new project here"** first. Enter on a known project attaches the tab (reason
`picker`) and then opens its Switchboard (or asks the one-time init question when it has none, or
files the held `/card`); Enter on the init row attaches the tab to the pane's directory, creates the
board through `board_init {git_init: true}` — `git init` too, unless the directory is already inside
a repository, which is then left exactly as it is — with no second question, and the created line
says what git did. An attached tab wears a chip with its project's name; one click detaches; an
unattached tab shows nothing. Options › Agent › Switchboard lists the known projects (why, when;
Remove forgets the record only) and the declined ones (Undo). The Switchboard's gear page offers
"Hide/Show this board's folder". The Sessions pane's Project chooser lists any project, each known
project, or no project, and the worker answers by workspace folder (`project` / `outside_projects`).

## QA checklist

- [ ] `ctest --test-dir build -R 'projectpicker|projects|projectinit|boardworkspace|boardsections|conversations|panestatus'` passes, and `./scripts/test.sh` (the `board_init … git_init` case is in `tests/test_board_protocol.py`, the `project_filter …` / `project_and_outside_projects …` cases in `tests/test_conv_index.py`).
- [ ] Run `docs/qa_evidence/2026-09-19-project-picker/drive.sh [build-dir]` (Xvfb, isolated HOME/XDG_*/TMPDIR, a fixture registry with two known projects, one declined path and a loose `Downloads`): `implementer-01-picker.png` shows the Projects pane beside the terminal with "＋ Initialize new project here" first and the two known projects under it, most recently attached first, each with its reason and age.
- [ ] `implementer-02-picker-filtered.png`: typing `wid` leaves the init row on top and selects `widgetworks`; Enter attaches the tab and opens its Switchboard, and the tab wears the chip "widgetworks" at the left of its label (`implementer-03-attached-chip.png`).
- [ ] The gear after the section checkboxes opens the section editor, which says the board is kept in `.switchboard/` and offers **Show this board's folder** (`implementer-04-gear-folder.png`); clicking it renames the folder to `switchboard/` (git mv in the fixture checkout), the notice line says so, the board keeps its card, and `implementer-disk.txt` lists `switchboard` under widgetworks with a clean `git status` (`implementer-05-folder-shown.png`). On an `issues/` board there is no such button.
- [ ] Clicking the chip detaches: the chip is gone, the status line says "This tab is no longer attached to widgetworks.", the hint teaches the palette, the Switchboard pane stays open (`implementer-06-detached.png`).
- [ ] The Sessions pane (Ctrl+Shift+Y) has a Project chooser after Kind — "Any project", `widgetworks`, `notes`, "No project" (`implementer-07-sessions.png`); choosing `widgetworks` lists the two sessions whose workspace is that folder or its `sub/` and the scope menu reads "All projects" (`implementer-08-sessions-project.png`).
- [ ] Ctrl+Shift+S again in `~/Downloads`, Enter on the init row: the pane prints "Switchboard created in …/Downloads/.switchboard/ · git repository initialized", the Switchboard opens, and on disk `Downloads/.switchboard/board.yaml` and `Downloads/.git` exist (`implementer-09-init-here.png`, `implementer-disk.txt`).
- [ ] Options › Agent › Switchboard (search "known") lists the known projects with "known because …, N days ago · last attached …" and a Remove button, and the declined path with Undo (`implementer-10-options-known.png`); Remove drops the first row (Downloads) from the list and the registry, and its files — `.switchboard/` and `.git` — are untouched (`implementer-11-options-removed.png`, `implementer-disk.txt`).
- [ ] With a real provider key: `/card remember this` in `~/Downloads` opens the picker; picking a project files the card there; "Initialize new project here" files it as the new board's first card.
