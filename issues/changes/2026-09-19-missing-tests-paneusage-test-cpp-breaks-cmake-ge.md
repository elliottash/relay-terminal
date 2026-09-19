---
id: GMSC
type: work
status: inbox
labels: [bug, build]
rank: zzzzzzr
created: '2026-09-19'
source: 'pane 1, 2026-09-19, during #H7N4'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Missing tests/paneusage_test.cpp breaks cmake generate for the whole tree

## Issue
Unrelated fault noticed while building: CMakeLists.txt adds tests/paneusage_test.cpp (relay-paneusage-tests, line 590) but the file does not exist in the tree or in HEAD, so `cmake --build build` fails at the generate step ("Cannot find source file: tests/paneusage_test.cpp") until the pane-usage work lands.
