---
id: 2DP8
type: work
status: needs-verification
labels: [feature, workflow, land]
parent: 3MH4
discovered_from: 3MH4
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
source: 'Approved #3MH4 implementation workstream, 2026-09-26'
assignee: codex
verify: {artifact: code, primary: script, also: [probe], human: none, effort: high}
links: {plans: [], commits: [], evidence: [tests/test_parallel_project_adoption.py], related: [], github: null}
---
# C2: Second-project adoption and operating documentation

## Issue
Exercise adoption using only project configuration in a second repository; write operating, migration and rollback documentation.

## Done means
An independent temporary Python repository uses one `.relay/project.toml` and the shipped CLIs to create and land verified work, reject a bad candidate, retain later edits, restart its queue, run the installed main release and roll back without disturbing the source checkout.
The operating and migration guides give runnable commands, state paths, safety limits and a cutover/rollback checklist backed by measured time and disk use.

## Plan
1. Read the stabilized CLI and project configuration; build a focused end-to-end test around an independent temporary repository.
2. Run the test, measure its actual build/install time and source/tree/build bytes, and document commands and behavior.
3. Run the landing check for only C2 paths, then land and record evidence on this card.

## Tasks
- [x] Exercise adoption and rollback through shipped CLIs. <!-- t:a1 -->
- [x] Finish user-facing operation and migration documentation. <!-- t:a2 -->
- [x] Record measured results and focused test evidence. <!-- t:a3 -->

## Tests
`python3 -m unittest tests.test_parallel_project_adoption -v` — passed in the shared working copy with B1's in-progress service; one independent temporary Python Git repository, two authors, bad gate, post-submit edit, Board job, installed executable and rollback. Measured on 2026-09-26: source 2,093 B, tree 2,205 B, build 6 B (cache total 2,208 B), installed release 1,929 B; two landing ticks 0.913 s and 0.990 s, including main updates of 0.185 s and 0.308 s.
`python3 scripts/land.py try c2-2dp8 --verify-cmd 'python3 -m unittest tests.test_parallel_project_adoption -v'` — passed on the exact candidate tree after B1 landed; test ran in a separate temporary repository and state/cache roots.
`git diff --check -- <C2 owned paths>` — passed.

## Execution Summary
The shipped CLI adopted a separate Python project from one `.relay/project.toml`, verified and landed two authors, rejected a bad candidate, retained later edits and protected dirty/unlanded/ignored workspaces. The test restarted the queue between steps, ran the old installed executable through an update, checked the new installed SHA and artifact, published a canonical Board snapshot, observed main lag, and rolled back without discarding checkout data. The operating and migration guides record commands, state paths, policy acceptance, supervisor operation, limits, measured costs and safe rollback. The Relay source repository remains in legacy mode.
