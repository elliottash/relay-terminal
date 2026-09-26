---
id: NQTD
type: work
status: needs-verification
labels: [bug, scratch]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: ed6889a9-4d9d-4075-9b39-8c8c0dc3cc15
rank: zzzzzzzzzzzzzzzzzz
created: '2026-09-25'
source: 'sweep note in the #WZ3K pane, 2026-09-25'
links: {plans: [], commits: [dc68472], evidence: [tests/test_scratch_ledger.py], related: [DVV2, WZ3K], github: null}
---
# Scratch sweep calls an old directory new when something inside it changed (~/.cache flagged)

## Issue
These unledgered entries in the temp dir or directly under your home directory appeared during your turn:
- /home/elliott/.cache
Relay owns agent scratch (#DVV2): ledger each one (a directory you made: scratch_dir, or relay-scratch adopt), move it into the project, or delete it. Nothing is deleted for you.

## Done means
A directory that already sat directly under `$HOME` or in the temp dir when the turn began (`~/.cache`, `~/.claude`, `~/.local`) is never named by the post-turn sweep, however much changes inside it during the turn. An entry that truly appears during the turn is still named.
Failure looks like: a sweep note listing `~/.cache` (or any directory whose birth time predates the turn), or a newly created top-level scratch dir going unreported.
Cause: the sweep read `st_mtime >= turn_start` as "created", and a directory's mtime changes whenever an entry is added to or removed from it.

## Tests
- `tests/test_scratch_ledger.py::ReportTests::test_an_entry_there_before_the_turn_is_not_new_whatever_its_mtime`: a pre-existing `~/.cache` whose mtime is bumped by a child write is not reported when the start-of-turn list is passed; a new sibling is; and without that list the old false positive comes back, so the test really covers the cause. The file ran 33 tests; the only failure is #B53G, which also fails before this change.
- `python3 -m py_compile backend/relay_core/agent.py` OK. No C++ touched, so no build needed.
