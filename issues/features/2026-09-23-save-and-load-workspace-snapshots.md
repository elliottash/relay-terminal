---
id: 6WKR
type: work
status: inbox
labels: [feature, sessions]
assignee: ''
rank: m
created: '2026-09-23'
source: 'Owner in a Relay pane, 2026-09-23'
links: {plans: [], commits: [], evidence: [], related: [N6R8], github: null}
---
# Save and load workspace snapshots

## Issue
can you save the current workspace so if it doesnt work, i can still restore manally? and add a card for a save/load workspace function

## Done means
Relay offers an explicit way to save a named workspace snapshot and load it later. A snapshot preserves the window, tab and pane arrangement, each pane's conversation association, and the available terminal scrollback and prompt history. Loading shows what will be replaced, does not silently erase the current workspace, and reports missing or unreadable session data before changing the layout. The snapshot is a portable file or folder the owner can keep and restore manually if Relay cannot start normally.

## Planning notes
The current automatic restore uses one `state/windows.json` layout and separate scrollback and session files. Design the save/load action around those existing formats, with a versioned manifest and an atomic restore path. Cover a failed restart and a snapshot containing session files that are no longer present in the live store.
