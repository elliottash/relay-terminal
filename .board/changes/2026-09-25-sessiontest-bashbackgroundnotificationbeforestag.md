---
id: JK3T
type: work
status: inbox
labels: [bug, tests, terminal]
discovered_from: HEY7
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
source: pane 2741413a, 2026-09-25
links: {plans: [], commits: [], evidence: [], related: [HEY7, BGSP], github: null}
---
# SessionTest::bashBackgroundNotificationBeforeStagedCommand fails at HEAD

## Issue
At main 2253e869, `relay-engine-tests` fails `SessionTest::bashBackgroundNotificationBeforeStagedCommand(libvterm)` on every run (3 of 3): `'state()["event"] == "ready" && !s.termiosFlags().canonical' returned FALSE`. Measured in a clean land.py verify slot of the tip itself, with no uncommitted changes. Found while verifying #HEY7's engine hook; the test was last touched in b352aa9e (#BGSP).
