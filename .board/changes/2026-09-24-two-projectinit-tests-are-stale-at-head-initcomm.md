---
id: FV0P
type: work
status: inbox
labels: [bug, tests, switchboard]
rank: zzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
source: 'pane 1, 2026-09-24 (found while landing #NSYT)'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Two projectinit tests are stale at HEAD: InitCommand in Pane.h, window text in RelayWindow.h

## Issue
While verifying #NSYT (2026-09-24): two assertions in tests/projectinit_test.cpp fail at HEAD itself, unrelated to any working-tree edit — `thePaneRaisesEveryTriggerAndBlocksNone` expects `Trigger::InitCommand` in src/Pane.h (0 occurrences at HEAD), `theWindowOffersItPassivelyAndRemembersTheAnswer` expects `Initialize a project here…` in src/RelayWindow.h (0 occurrences at HEAD; the string lives in src/Pane.h and src/RelayWindow.cpp). Measured: `git show HEAD:src/Pane.h | grep -c "Trigger::InitCommand"` → 0, same at HEAD for RelayWindow.h; `./build/relay-projectinit-tests` → 20 passed, 2 failed.
