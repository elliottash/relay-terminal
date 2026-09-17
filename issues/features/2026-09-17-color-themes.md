---
id: 0JA7
type: work
status: ready
component: [gui]
milestone: desktop-alpha
workstream: terminal
rank: zz0j
created: '2026-09-17'
acceptance: switching theme in Settings restyles the app, the terminal and the composer colours without a restart
source: '`issues/feature_intake.txt`, 2026-09-17: "allow different color themes."'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Colour themes

Ship a few built-in themes (the current dark, a light one, and one or two popular palettes) selectable in
Settings, applied to the app chrome, the terminal colour scheme and the composer's syntax colours together.
Themes are plain files so people can add their own; the engine and KonsolePart must use the same one.
