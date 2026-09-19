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
acceptance: Ctrl+Shift+S in a pane with no candidate project offers known projects and the inbox; an attached tab shows its project and detaches in one click; an inbox card moves to a project
source: 'owner, 2026-09-18, designing the project model after #JN7X: a tab has at most one project; "i use warp a lot, just screwing around in my downloads or admin folders"; agreed to a "move to project…" action on inbox cards ("lets try it that way")'
links: {plans: [], commits: [], evidence: [], related: [JN7X], github: null}
---
# A project picker, a personal inbox and a chip on the tab that says which project it is attached to

## Issue

Stage 4 of the project model (the earlier stages are on `main`: `6a103c4`, `bdd53c0`, `c1c5fad`, and the
init question). What is missing is everything a person *sees* about attachment, and somewhere for
a card to go when there is no project.

## Decisions (owner, 2026-09-18)

1. Ctrl+Shift+S in a pane with **no candidate project** (`~/Downloads`, an admin folder) opens a
   picker: the known projects (`relay::projects::Registry::knownProjects()`, most recently attached
   first) plus a **personal inbox** for loose cards. Picking one attaches the tab (reason `picker`).
   A pane, not an overlay.
2. The inbox is the one board that lives in Relay's data directory
   (`relay::projects::inbox()`, `$XDG_DATA_HOME/relay/boards/inbox/switchboard`). It is created by
   its first card. Proposed, not yet confirmed by the owner: make that folder a small git repository
   Relay commits to after each write, since nothing else backs it up.
3. A card in the inbox gets a **"Move to project…"** action. If the target project has no
   Switchboard yet, the move asks the one-time init question first.
4. An attached tab shows a **chip** with the project's name; one click detaches. An unattached tab
   shows nothing at all — no "not attached" state.
5. The known-projects list is **removable** (`Registry::forget`) and shows why each project became
   known (`Record::reason`), from the Options pane. Lesson from Warp's #11899, where the list only
   grows.
6. The tab list and the conversation list **group by project** from the start (Warp's most-asked
   missing feature, #9875).

## Tasks

- [ ] `src/ProjectPicker.*` as a pane, fuzzy-filtered with `relayFuzzyScore`, offscreen widget test <!-- t:2g -->
- [ ] the inbox board: worker pointed at it with `set_board {dir}`, created by the first card <!-- t:yh -->
- [ ] "Move to project…" on a card: copy the card file and its thread, close the original with a link <!-- t:6r -->
- [ ] the tab chip and one-click detach; remove the palette-only detach once the chip exists <!-- t:mn -->
- [ ] known projects in Options: list, reason, forget; declined projects: list, undo <!-- t:d2 -->
- [ ] group tabs and conversations by `relay::projects::keyFor` <!-- t:mg -->
- [ ] shortcut hints for whatever gains a key (WARP.md standing rule) <!-- t:y9 -->
