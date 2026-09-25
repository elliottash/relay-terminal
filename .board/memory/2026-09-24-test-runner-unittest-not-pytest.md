---
id: ANRZ
type: memory
status: active
rank: i
created: '2026-09-24'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Test runner: unittest, not pytest

## Issue
System python3 on this machine has no pytest module (a2 and the R5TC session both hit this). Run the backend test modules with `python3 -m unittest tests.<module>` — e.g. `python3 -m unittest tests.test_panes` (17 tests), `python3 -m unittest tests.test_board_tools` (312 tests).
