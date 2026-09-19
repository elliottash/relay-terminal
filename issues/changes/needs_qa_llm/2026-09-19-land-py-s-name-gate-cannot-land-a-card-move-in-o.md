---
id: DJX7
type: work
status: needs-qa-llm
labels: [bug, switchboard]
implemented_by: glm/glm-5.3
rank: zzzzzzr
created: '2026-09-19'
source: 'pane, 2026-09-19, while landing #N8VK'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-land-py-card-move-one-commit/], related: [], github: null}
---
# land.py's name gate cannot land a card move in one commit (rename collapse)

## Issue
Landing a card move (delete old path + add new path in one land.py commit) always fails the name gate: `git diff --name-only tip new` collapses the pair into one rename line (R096, the card file is 96 % similar), so `touched` lists only the destination while `entries` lists both — "name gate failed … Nothing was landed". Measured 2026-09-19: a commit whose tree verifiably lacks the old path (ls-tree empty) still diffs as a single rename. Workaround used: land the move as two commits (add, then delete). Fix: pass `--no-renames` to the gate's `git diff --name-only` (scripts/land.py, cmd_commit).

## QA checklist
Fixed in `de3510f` (all three `git diff --name-only` gate diffs in `scripts/land.py` pass `--no-renames`; regression test in `tests/test_land.py`).

- [ ] `verify.log`: the three `--no-renames` call sites at HEAD (lines 1420, 1660, 1727); `tests/test_land.py` green in the full `./scripts/test.sh` run.
- [ ] End-to-end: a card move (delete + add of the same bytes) lands in one `land.py commit` without a name-gate failure — done live by #5G43's move in this same landing.

Evidence: docs/qa_evidence/2026-09-19-land-py-card-move-one-commit/
