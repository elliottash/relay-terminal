---
id: DT6Z
type: work
status: needs-verification
labels: [bug, tooling]
assignee: agent
implemented_by: glm/glm-5.3
session: 2381c59f-e196-45ee-bf7f-8664aa15f44d
rank: zzzzzzzzzzzzzzzzzzw
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [probe], human: none, criteria: tests.test_land passes; a held review with a selection names the unselected paths, sign_off: none, effort: low, stakes: rework, blast: capability}
source: pane 1, 2026-09-24 (session kdb4)
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-24-land-only-hunk-scope/], related: [], github: null}
---
# land.py: the review lets --only-hunk read as narrowing the whole commit

## Issue
*(Re-diagnosed 2026-09-24 18:40 by the filing session; the original text — kept in the thread — claimed the verify tree ignored `--only-hunk`. That mechanism was wrong.)*

`--only-hunk <path>:<n>` narrows **that one path's hunks**; every other claimed path lands whole. The tool's own output invites the wider reading, and a session (the filer's) followed it end to end:

1. Two paths claimed (`src/Pane.h`, which held another session's uncommitted #VD2M hunk, and `tests/test_conv_index.py`); the commit was held for review and digest `270315c68577` printed for **both** paths.
2. Re-ran with `--only-hunk tests/test_conv_index.py:1 --confirm 270315c68577` — the selection named the test path only, so `src/Pane.h` stayed wholly selected; plans, and therefore the digest, were unchanged, and `--confirm` matched.
3. The verify gate built tip + both paths — the tree the digest pins — and failed on the other session's half-hunk, refusing a commit that would have landed it.

The gate and the materialisation behaved as designed: the tree is built from the selected hunks and the digest covers the exact bytes of every path in it. What failed was communication. `land all of it: rerun **the same command** with --confirm` suggests the digest is tied to the command; `--only-hunk … (lands only those)` suggests it narrows the whole landing; and nothing in the review says that paths no selection names are still wholly in the commit.

The hazard is real in both directions: here the gate only wasted a verify cycle on a refused commit, but a session convinced `--only-hunk` had narrowed the tree can equally confirm a digest that lands a path it never reviewed.

## Done means

- A held review that names a `--only-hunk`/`--exclude-hunk` selection also names every path the selection does not cover, as still wholly in the commit.
- The confirm hint says the digest pins the tree (paths + bytes), not the flags it was reached with — no more "rerun the same command".
- The `--only-hunk` help line states its scope is one path.
- Selection, digest and tree semantics are unchanged: a regression test pins that confirming a held digest with a selection lands exactly the tree that was printed — selected hunks on named paths, whole contents on the rest.

## Execution Summary

Landed `622e7506`.

- `print_review()` takes an `unselected` list and, when any selection is active, prints `still wholly in this commit — no selection named them: <paths>` right under the "what it would land" list. `cmd_commit` computes it per attempt (`plans` minus the paths named by `--exclude-hunk`/`--only-hunk`).
- The confirm hint is now `rerun with --confirm <digest> (the digest pins the tree printed above, not the flags you reached it with)` — the old "the same command" was the sentence that misled the filer into adding `--only-hunk` under a held digest.
- The `--only-hunk` line reads `(those hunks of that one path only — other paths land whole, as listed above)`.
- No behaviour change to selection, digest, tree or gate.
- Developed in a scratch copy with `RELAY_LAND_SCRIPT`, per the suite's own header rule, then moved into `scripts/` once green.

## Tests

`python3 -m unittest tests.test_land`: **64 passed** (2 new) — against the scratch with `RELAY_LAND_SCRIPT`, then again against `scripts/land.py` after the move.

- `test_a_selection_names_one_paths_hunks_not_the_whole_commit` — two contested paths, `--only-hunk f.txt:1`: the review names `big.txt` as wholly in the commit and the new hint wording; confirming the digest lands f.txt's hunk **and** big.txt whole (the tree the digest pinned).
- `test_no_selection_line_when_no_selection_was_given` — the new line appears only when a selection is active.
