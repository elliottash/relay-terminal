---
id: QAJQ
type: work
status: inbox
labels: [bug]
rank: zzzzzzzzw
created: '2026-09-19'
source: 'pane 2 (execute #T7AH), 2026-09-19'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# tabProjectChip stylesheet sits under the 9pt font floor (buttonfit fails at HEAD)

## Issue
Unrelated fault noticed while executing #T7AH: `ctest --test-dir build` fails `buttonfit` at HEAD — `ButtonFitTest::stylesheetFontsStayAtOrAboveTheFloor()` reports `dark-copper: "font-size: 8.5pt" is under the 9pt floor`, from `QToolButton#tabProjectChip` at src/Theme.cpp:542 (present at HEAD 8b260cff, untouched by #T7AH's one-line terminal.conf diff). Measured: ctest run 2026-09-19, 1 of 63 tests failed; all other 62 pass.
