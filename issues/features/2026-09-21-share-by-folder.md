---
id: SHP7
type: work
status: needs-verification
labels: [feature, composer]
assignee: codex
rank: m
created: '2026-09-21'
source: 'Codex in Relay, 2026-09-21'
links: {plans: [], commits: [42b1f54ff2345ae6a9f0bf01caf1d067c733367b], evidence: [docs/qa_evidence/2026-09-21-share-by-folder/], related: [], github: null}
---
# Move pane sharing beside the folder

## Issue
how about we move the share button down next to the folder, rather than where it is at the top right

## Plan
Move the existing share action and live state into the composer strip immediately after the folder. Remove the chrome share button and its L-shaped panel outline. Build and inspect the GUI with isolated settings.

## Execution Summary
Moved sharing into the prompt strip immediately after the folder path, preserving click behavior, live shared color and guest tooltip. Removed the top-right share button and its obsolete L-shaped mask. Updated the phone tooltip to point at the new location.

## Tests
manual: docs/qa_evidence/2026-09-21-share-by-folder/notes.md

## QA checklist
- Confirm sharing is immediately right of the folder in the prompt strip, with no duplicate at top right.
- Click sharing in an unshared pane and verify the sharing dialog opens.
- On a shared pane, verify color/guest tooltip updates and clicking opens Sharing.
- Check narrow panes and plan mode retain a usable strip layout.
