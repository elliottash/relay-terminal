---
id: BTYE
type: work
status: needs-qa-llm
labels: [bug, tooling, switchboard]
component: [worker]
implemented_by: Claude Fable 5.1, 2026-09-19
rank: zzzzzzzi
created: '2026-09-19'
acceptance: a commit that deletes one claimed path and adds another lands without a workaround, and a test in the script's own suite moves a file
source: 'found by the #T71W session (Claude Fable 5.1 in Claude Code), 2026-09-19, landing 7d1a7bc'
links: {plans: [], commits: [de3510f], evidence: [], related: [T71W], github: null, merged_from: [DJX7]}
---
# land.py's name gate refuses a moved file: git reports a rename as one path

## Issue
scripts/land.py refuses a commit that moves a file: its name gate compares `git diff --name-only TIP NEW` with the session's paths, and git's rename detection reports a moved file as its new path only, so the old path is "missing" and nothing lands. Seen 2026-09-19 moving card #T71W from issues/features/ to issues/features/needs_qa_llm/ (every card's move to a QA lane is this shape):

land.py: name gate failed: the commit would touch ['docs/VALIDATION.md', 'issues/features/needs_qa_llm/…', 'issues/threads/T71W.md'] but this session's paths are [… the same three plus the old path …]. Nothing was landed.

Workaround that landed it (7d1a7bc) without touching the script: GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=diff.renames GIT_CONFIG_VALUE_0=false python3 scripts/land.py commit <me> -m …  The fix is one flag: pass --no-renames to that `git diff --name-only` (and to any other name comparison in the script).

## Resolution (found already fixed by the 2026-09-19 board sweep)

`de3510f`, "land.py: a moved file lands in one commit (the name gate ignores rename detection)".
The fix is the one this card named: all three of the script's name-comparison diffs now pass
`--no-renames` (`cmd_commit`'s gate, `cmd_repair`'s gate, and the changed-set diff), so a delete
plus an add of the same bytes is listed as two paths rather than collapsed into one rename line.

The acceptance asked for a test in the script's own suite that moves a file, and there is one:
`tests/test_land.py::test_a_moved_file_lands_in_one_commit`. It fails on the previous version.

**#DJX7 is the same fault** and was filed separately the same day (already recorded on its thread by
`59c8e82`). Its card file is still uncommitted in the shared tree and belongs to another session, so
this sweep did not touch it — whoever owns it can close it against this commit.

## QA checklist
- [ ] Move a card to a QA lane in one `land.py commit` (delete the old path, add the new): it lands, with no `GIT_CONFIG_*` workaround.
- [ ] `python3 -m pytest tests/test_land.py` passes.
- [ ] A `repair` that touches a moved path is still gated correctly.

## Merged in
### #DJX7 — land.py's name gate cannot land a card move in one commit (rename collapse) (2026-09-19)

Merged from `issues/changes/needs_qa_llm/2026-09-19-land-py-s-name-gate-cannot-land-a-card-move-in-o.md` (needs-qa-llm): Same fault, same fix: land.py's name gate collapsed a card move into one rename line; both name fix de3510f (--no-renames on the gate diffs), and #BTYE's resolution already records #DJX7 as the same fault filed the same day.

#### Issue
Landing a card move (delete old path + add new path in one land.py commit) always fails the name gate: `git diff --name-only tip new` collapses the pair into one rename line (R096, the card file is 96 % similar), so `touched` lists only the destination while `entries` lists both — "name gate failed … Nothing was landed". Measured 2026-09-19: a commit whose tree verifiably lacks the old path (ls-tree empty) still diffs as a single rename. Workaround used: land the move as two commits (add, then delete). Fix: pass `--no-renames` to the gate's `git diff --name-only` (scripts/land.py, cmd_commit).

#### QA checklist
Fixed in `de3510f` (all three `git diff --name-only` gate diffs in `scripts/land.py` pass `--no-renames`; regression test in `tests/test_land.py`).

- [ ] `verify.log`: the three `--no-renames` call sites at HEAD (lines 1420, 1660, 1727); `tests/test_land.py` green in the full `./scripts/test.sh` run.
- [ ] End-to-end: a card move (delete + add of the same bytes) lands in one `land.py commit` without a name-gate failure — done live by #5G43's move in this same landing.

Evidence: docs/qa_evidence/2026-09-19-land-py-card-move-one-commit/
