---
id: 4PW5
type: work
status: ready
component: [gui]
milestone: desktop-alpha
workstream: terminal
rank: zzn1
created: '2026-09-17'
labels: [bug]
acceptance: a pane created by the split key or the pane button takes the keyboard at once — typing goes into its prompt box and Ctrl+W closes it
source: 'owner in chat, 2026-09-17: "after creating a pane, its not active yet, i cant type anything or ctrl + w to kill it."'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# A new pane is not active: typing and Ctrl+W do nothing

## Report

Owner, 2026-09-17, Relay engine as default: after making a pane it cannot be typed into and Ctrl+W does not
close it, so the window does not treat it as the active pane.

## Notes from a first look

`RelayWindow::split()` already calls `setActive(pane)` and, on the next event-loop turn, `pane->focusInput()`.
Suspects, in order:
1. `focusInput()` is a no-op for a pane that has not finished starting: a pane begins in native mode (the
   composer hidden) until its shell reports a prompt, so the call lands on the terminal, which now refuses
   focus under the prompt-box-only rules (`Qt::NoFocus`, the FocusIn bounce) — leaving no focused widget.
2. The window-level key filter resolves actions against the focused widget's pane, so with no focus Ctrl+W
   finds nothing.
3. The engine view's `showEvent`/zero-timer geometry work may steal or drop focus while the pane is created.

Fix so the new pane's prompt box has the keyboard as soon as it exists, whatever the shell has done yet, and
add a regression check (a pure helper, or an Xvfb step that types into a fresh pane).
