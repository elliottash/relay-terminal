---
id: 9ACN
type: work
status: inbox
labels: [bug, models, tests]
rank: zzzzzzzzzzzzzzzzzzy
created: '2026-09-23'
source: '#HJ1T session, 2026-09-23'
links: {plans: [], commits: [], evidence: [], related: [N4PW, 00G1], github: null}
---
# Models pane narrow-layout test: the fifth tab overflows the tab bar

## Issue
(found by the #HJ1T session, not a user request) `xvfb-run -a build/relay-modelspane-tests -silent` on main at d6f769f6: 23 passed, 1 failed. `ModelsPaneTests::everyTabKeepsItsMainControlsInANarrowPane()`: `pane.tabBar()->tabRect(4).right() <= pane.tabBar()->width()` returned FALSE (tests/modelspane_test.cpp:235). The Jobs tab no longer fits in the narrow pane's tab bar.
