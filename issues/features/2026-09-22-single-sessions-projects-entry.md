---
id: SPSG
type: work
status: inbox
labels: [feature, keyboard, sessions, projects]
rank: m
created: '2026-09-22'
source: 'Owner in a Codex Relay pane, 2026-09-22; keyboard-system survey'
links: {plans: [], commits: [], evidence: [], related: [P7SJ, DKEW, KYPR, QWAS], github: null}
---
# Give Sessions and Projects one entry point and consistent toggling

## Issue
sessions and projects are a single function (if there are separate pathts right now, thats redundant)

this is great. we shoudl definitely fix the identified gaps so card those first.

## Planning notes
`src/RelayWindow.h` already hosts Projects, Sessions and Globals in one SessionManager pane, but the public actions remain separate: `projects.open` opens the Projects tab, while `agent.resume`/`conversations.open` toggle Sessions. `src/Keymap.h` assigns Ctrl+Shift+P and Ctrl+Shift+Y respectively. A focused Projects or Globals entry does not share Sessions' close-on-repeat behavior.

The owner considers Sessions and Projects one function. Consolidate public navigation to one Sessions & Projects entry and shortcut; internal filters or views may remain useful without separate top-level shortcuts. Preserve explicit project attachment, saved-session search, active-session navigation, Recently closed, and access to Globals. The exact key and treatment of Globals are part of the broader shortcut discussion, not settled by this card.

## Decisions
Owner: "sessions and projects are a single function (if there are separate pathts right now, thats redundant)".
Owner: "nobody has used relay yet so we can think fresh." No legacy-key migration is required merely to preserve shipped defaults.

## Done means
Sessions and Projects have one obvious public entry point and one shortcut, with no duplicate destination entries. The command opens, focuses, or closes the same pane predictably and retains its useful state; project/session operations remain available.

## Tests
Planned: `tests/conversations_test.cpp`, `tests/projectspane_test.cpp`, `tests/panetabnavigation_test.cpp`, `tests/test_keybindings.py`, and an isolated GUI check of shared-pane state, focus return, and repeated-key toggling.
