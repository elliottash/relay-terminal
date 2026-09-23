---
id: R7ST
type: work
status: inbox
labels: [feature, sessions, gui]
assignee: ''
rank: m
created: '2026-09-23'
source: 'Owner in a Relay pane, 2026-09-23'
links: {plans: [], commits: [], evidence: [], related: [K6KP, N6R8, HDA9, 6WKR], github: null}
---
# Restart Relay only after the old instance exits

## Issue
should we also add a /restart command that will safely restart after the previous instance ends

## Done means
`/restart` is a local Relay command in the prompt box and is discoverable in command help and Actions. It saves the current window layout and terminal scrollback through the normal clean-quit path, lets the old Relay process fully exit and release its layout lock, and only then launches the replacement. The replacement restores the same workspace without a `--fresh` or explicit `--workspace` argument. A failed launch leaves the saved state intact and gives the owner a clear way to start Relay manually. The command handles other open Relay windows and does not silently discard running agent work.

## Planning notes
The current `/update` implementation in `RelayWindow::updateApp()` launches the replacement before closing the old windows. Share a safe relaunch mechanism with `/update` rather than leaving the two paths with different ordering. The overlapping-process layout overwrite was measured and fixed separately in #K6KP; this feature should still avoid overlap entirely and verify the exact sequence in an isolated profile.
