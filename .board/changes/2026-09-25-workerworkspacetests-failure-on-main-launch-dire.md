---
id: 6AZW
type: work
status: inbox
labels: [bug, worker]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
source: 'Relay pane (card #NXN0 verification), 2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# WorkerWorkspaceTests failure on main: launch directory board never adopted

## Issue
`tests/test_board_protocol.py::WorkerWorkspaceTests::test_the_launch_directorys_board_is_never_adopted_by_a_workspaceless_configure` fails on a clean export of `a7cc1fcc` (before card #NXN0's work; verified 2026-09-25): `the worker's cwd has a board` — the test expects the worker's launch directory to have a `.board/board.yaml` and it does not. Pre-existing on main, unrelated to card turns; left for whoever owns worker workspace adoption.
