---
id: GMSC
type: work
status: done
labels: [bug, build]
rank: zzzzzzr
created: '2026-09-19'
source: 'pane 1, 2026-09-19, during #H7N4'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Missing tests/paneusage_test.cpp breaks cmake generate for the whole tree

## Issue
Unrelated fault noticed while building: CMakeLists.txt adds tests/paneusage_test.cpp (relay-paneusage-tests, line 590) but the file does not exist in the tree or in HEAD, so `cmake --build build` fails at the generate step ("Cannot find source file: tests/paneusage_test.cpp") until the pane-usage work lands.

## Resolution
Done 2026-09-19: the pane-usage work landed. `tests/paneusage_test.cpp` is in the tree (13,625
bytes) and `CMakeLists.txt:604` builds `relay-paneusage-tests` from it, so `cmake -S . -B build`
configures and `cmake --build build` builds. The card's line number had drifted too — 604, not 590.
Filed and fixed by #D03W; nothing to do here.
