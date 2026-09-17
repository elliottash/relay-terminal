---
id: X2F1
type: work
status: ready
component: [gui]
milestone: desktop-alpha
workstream: terminal
rank: zzx2
created: '2026-09-17'
acceptance: an engine pane's right-click menu covers the Konsole items worth keeping, with the dropped ones listed in the card
source: '`issues/feature_intake.txt`, 2026-09-17: "compare the right click context menus we had in the konsole engine to see if there is anything we should bring in here."'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Bring the useful Konsole context-menu items to the engine panes

Compare KonsolePart's right-click menu with the engine pane's and port what is worth having: copy, paste,
select all, clear scrollback and reset, search, open link / copy link address, open file at this path,
zoom in/out/reset, change profile bits, "Save output as…", and split/close pane. Keep Relay's own entries
(open the turn, take control, tasks) in the same menu, and list anything deliberately dropped.
