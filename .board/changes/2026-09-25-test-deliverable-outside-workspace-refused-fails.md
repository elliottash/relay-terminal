---
id: B53G
type: work
status: inbox
labels: [bug, tests]
rank: zzzzzzzzzzzzzzzzzw
created: '2026-09-25'
source: 'agent session ed6889a9, 2026-09-25, while running #WZ3K'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# test_deliverable_outside_workspace_refused fails at HEAD: guard note reworded by #DVV2, test wording left behind

## Issue
Unrelated fault noticed while running #WZ3K: at clean HEAD (export verified, commit 0ccf9604ccc4), tests/test_scratch_ledger.py WriteGuardTests.test_deliverable_outside_workspace_refused fails: assertIn("deliverable", refusal) — the write guard's temp-dir refusal text no longer contains the word "deliverable"; it reads ".../report-out.md is inside the system temp dir. Relay owns agent scratch (#DVV2): ask for a directory with scratch_dir ...". The #DVV2 rewording of the guard note (commits 0da66145/acdbb2d0) left this test's expected wording behind. Reproduce: PYTHONPATH=backend python3 -m unittest discover -s tests -p "test_scratch_ledger.py" — 1 failure of 27 at HEAD, unchanged with #WZ3K's edits (32 tests, same single failure).
