---
id: 27AR
type: work
status: needs-verification
labels: [bug, scratch, guest]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 1f8636de-3949-4f85-ab45-2824412ec7bf
rank: zzzzzzzzzzzzzzzzzz
created: '2026-09-25'
source: scratch watcher, pane 69d471f7, 2026-09-25
links: {plans: [], commits: [b5a36996], evidence: [tests/test_scratch_ledger.py], related: [WZ3K, DVV2, NQTD, XY13], github: null}
---
# Scratch watcher flags Relay's own per-session guest bridge dirs (/tmp/relay-*) every turn

## Issue
These unledgered entries in the temp dir or directly under your home directory appeared during your turn: /tmp/relay-FyJqoI, /tmp/relay-eEyklM, /tmp/relay-gekdhn — Relay owns agent scratch, ledger each one, move it into the project, or delete it.

## Done means
The post-turn scratch sweep never names a directory Relay itself made: a pane's `/tmp/relay-XXXXXX` / `relay-open-XXXXXX` (src/RuntimeDirs), a guest's `/tmp/relay-board-*` board-bridge socket dir (backend/relay_core/guest_board_bridge.py) or a `tests_run` `/tmp/rt-*` dir (backend/relay_core/tests_protocol.py), however many panes open or runs start during the turn. The rule is the RuntimeDirs owner mark (an `owner` file whose first line is `relay-owner 1`), not the name, so an agent's own `rt-foo` dir, a forged `owner` file or a symlink to a marked dir is still named.
Failure looks like: a sweep note listing `/tmp/relay-…` after a backend restarted on b5a36996 or later.

## Tests
- `tests/test_scratch_ledger.py::ReportTests::test_relay_owned_runtime_dirs_are_never_named`: a dir with the C++ mark byte for byte and one marked by `scratch.mark_relay_owned()` are skipped; a forged `owner` file, a symlink to a marked dir and an unmarked `rt-agent-scratch` are still named; the mark is 0600, carries this pid and starttime, and leaves no `.tmp` behind.
- `test_guest_board_bridge_marks_its_socket_dir`: a real `Bridge()` carries the mark.
- `python3 -m unittest tests.test_scratch_ledger`: 36 run, 1 failure, `test_deliverable_outside_workspace_refused`, which is #B53G and fails at HEAD too. `tests.test_tests_protocol tests.test_guest_board_bridge`: 124 run, 1 failure, `test_native_catalog_parity_and_relay_policy_dispatch`, which also fails on a clean `git archive HEAD` export.
- On the real machine, a 3-hour window over live `/tmp` and `$HOME` against a copy of the ledger without the pause rows: 43 entries without the rule, of which 25 were pane runtime dirs; 18 with it, of which 0 were. The 5 `relay-board-*` still named were made by bridges running pre-fix code, so they have no mark yet. The rest are pre-existing home dotfiles and desktop-session entries, which a real turn drops through #NQTD's turn-start list.
- Not done: the live turn-end check in a restarted backend. The pause rows sc4662e/scab630 are still in the ledger (see the decision entry) because the running workers load the pre-fix sweep until Relay restarts.
