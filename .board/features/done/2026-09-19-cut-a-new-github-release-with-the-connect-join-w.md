---
id: 3AZG
type: work
status: done
labels: [feature, packaging, remote]
rank: zzzzzx
created: '2026-09-19'
source: pane 1, 2026-09-19
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Cut a new GitHub release with the /connect (join) work so the laptop can install it

## Issue
upgrade the builds on github so i can /connect on my laptop.

## Resolution

Done as a release request: GitHub release `v0.1.0-beta.4`, published 2026-09-22, includes
`relay_0.1.0-beta.4_macos_arm64.dmg`, `relay_0.1.0-beta.4_macos_x64.dmg`, and a Windows x64
installer. The tagged source contains `/connect` as an alias of `/join` in `src/Pane.h`. This
proves the installable release exists; a particular laptop's installation or sign-in was not
observed and is not claimed here.
