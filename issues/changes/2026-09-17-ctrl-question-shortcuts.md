---
id: T9ZS
type: work
status: ready
component: [gui]
milestone: desktop-alpha
workstream: terminal
rank: zzt9
created: '2026-09-17'
labels: [bug]
acceptance: Ctrl+? opens the shortcuts overlay on the owner's keyboard, and the overlay lists the key that worked
source: '`issues/bug_intake.txt`, 2026-09-17: "ctrl+? doesnt work to show the shortcuts"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Ctrl+? does not open the shortcuts overlay

`help.shortcuts` is bound to Ctrl+? and F1. On the owner's keyboard Ctrl+? is Ctrl+Shift+/ and does not fire.
Bind the combination Qt actually reports (check Ctrl+Shift+/, Ctrl+/ and the keypad variants), verify in all
four presets, and make sure the palette entry and F1 still work.
