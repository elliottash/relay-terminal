---
id: DT6Z
type: work
status: inbox
labels: [bug, tooling]
rank: zzzzzzzzzzzzzzzzzzw
created: '2026-09-24'
source: pane 1, 2026-09-24 (session kdb4)
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# land.py: --only-hunk verify tree includes the hunks it was told to leave out

## Issue
Unrelated fault noticed while landing #KDB4 (not reported by the user): `scripts/land.py commit --only-hunk <path>:<n> --confirm <digest>` materialises the verify tree from the *unselected* paths' working copies too, so the build gate checks a tree the commit would not put on main.

Measured, 2026-09-24, session `kdb4` landing #KDB4 (reproduced twice, including after `rm -rf /tmp/claude-1000/land/kdb4/verify`):

1. `begin kdb4 --base main src/Pane.h tests/test_conv_index.py` — the working tree's `src/Pane.h` held another session's uncommitted hunk (#VD2M, `relay::screen::rowHoldsPrompt`).
2. `commit kdb4 -m … --only-hunk tests/test_conv_index.py:1 --confirm 270315c68577` — held for review, digest printed, then the same command with `--confirm`.
3. The verify step materialised tree `3da90aac16be` whose `src/Pane.h:14832` **contains `rowHoldsPrompt`** — the excluded hunk — and the gate failed with `error: 'rowHoldsPrompt' is not a member of 'relay::screen'`, refusing a commit whose actual tree (tip + one Python test hunk) would not have contained it.

`--paths tests/test_conv_index.py --confirm <new digest>` landed the same change correctly (`dff42e06`), so the materialisation follows the path subset but not the hunk subset.

Two directions matter: a gate that fails a good commit wastes a verify cycle (this case), and a gate that builds a different tree than the one it lands can equally pass a commit whose real tree does not build — the exact incident class the gate exists for (`#04EC`/CLAUDE.md "what gets built is what would land").
