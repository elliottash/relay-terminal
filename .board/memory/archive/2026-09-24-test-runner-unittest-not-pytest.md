---
id: ANRZ
type: memory
status: retired
rank: i
created: '2026-09-24'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Test runner: pytest installed, unittest as fallback

## Issue
pytest is installed on this machine's system python3 as of 2026-09-25 (owner-confirmed; it was missing when the a2 and R5TC sessions hit it). Run the backend tests with pytest as usual. `python3 -m unittest tests.<module>` remains the zero-dependency fallback for environments without pytest — e.g. `python3 -m unittest tests.test_panes` (17 tests), `python3 -m unittest tests.test_board_tools` (312 tests).
