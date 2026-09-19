---
id: 916B
type: work
status: ready
labels: [feature]
component: [gui, worker]
milestone: beta
workstream: switchboard
rank: '2'
created: '2026-09-19'
acceptance: Ctrl+Shift+S in a pane with no candidate project offers known projects; an attached tab shows its project and detaches in one click; a card filed in a tab with no project attached uses the default project (once set) or the picker
source: 'owner, 2026-09-18, designing the project model after #JN7X: a tab has at most one project; "i use warp a lot, just screwing around in my downloads or admin folders"; agreed to a "move to project…" action on inbox cards ("lets try it that way"). Revised owner, 2026-09-19: dropped the personal inbox board (see Decisions)'
links: {plans: [], commits: [], evidence: [], related: [JN7X], github: null}
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
(landed alongside the hidden-board-folder decision's C++ half, `9a3badec228f`, since both touched
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

- [x] the hidden-board-folder discovery order and the `defaultProject()` setting that replaces the inbox, in `src/Projects.h`/`.cpp` (`9a3badec228f`) <!-- t:hb -->
- [ ] `src/ProjectPicker.*` as a pane, fuzzy-filtered with `relayFuzzyScore`, offscreen widget test — known projects only, no inbox row, `defaultProject()` preselected when set <!-- t:2g -->
- [ ] the tab chip and one-click detach; remove the palette-only detach once the chip exists <!-- t:mn -->
- [ ] known projects in Options: list, reason, forget; declined projects: list, undo; alongside them, the "Hidden Switchboard folder" toggle and the "Default project for loose cards" row (both already have their `QSettings` keys and reader/writer functions — `board/hidden_folder`, `board/default_project` — only the Options rows are missing) <!-- t:d2 -->
- [ ] group tabs and conversations by `relay::projects::keyFor` <!-- t:mg -->
- [ ] shortcut hints for whatever gains a key (WARP.md standing rule) <!-- t:y9 -->
