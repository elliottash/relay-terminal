<!-- relay:entry 20260924T222038Z-pg author=agent kind=event model=glm-5.3 pane=0d5e4e19 turn=8fb546ceb6f847e9b234227209058e6a/1e42a89940b94b3cb8c8737ae01f7c08 -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-24-land-py-only-hunk-verify-tree-includes-the-hunks.md

<!-- relay:entry 20260924T222048Z-bz author=agent kind=event model=glm-5.3 pane=0d5e4e19 turn=8fb546ceb6f847e9b234227209058e6a/1e42a89940b94b3cb8c8737ae01f7c08 -->
- ✦ agent updated this card · appended to `## Issue`

<!-- relay:entry 20260924T223233Z-2z author=agent kind=event model=glm-5.3 pane=2381c59f turn=8fb546ceb6f847e9b234227209058e6a/4f0e6e95ba3a4214a50555daced32954 -->
- ✦ agent claimed this card · assignee agent, Inbox → Executing, implemented_by glm/glm-5.3, session 2381c59f

<!-- relay:entry 20260924T223233Z-c4 author=agent kind=progress model=glm-5.3 pane=2381c59f turn=8fb546ceb6f847e9b234227209058e6a/4f0e6e95ba3a4214a50555daced32954 pane_token=2381c59f-e196-45ee-bf7f-8664aa15f44d -->
Claimed (2381c59f) · working on it from a terminal pane

Fixing the --only-hunk verify-tree materialisation: the gate must build exactly the tree it would land.

<!-- relay:entry 20260924T223616Z-ty author=agent kind=event model=glm-5.3 pane=2381c59f turn=8fb546ceb6f847e9b234227209058e6a/4f0e6e95ba3a4214a50555daced32954 -->
- ✦ agent updated this card · title: land.py: --only-hunk verify tree includes the hunks it was told to leave out → land.py: the review lets --only-hunk read as narrowing the whole commit; replaced `## Issue`

<!-- relay:entry 20260924T223616Z-tz author=agent kind=rewrite model=glm-5.3 pane=2381c59f turn=8fb546ceb6f847e9b234227209058e6a/4f0e6e95ba3a4214a50555daced32954 -->
- ✦ rewrote title

<details><summary>before</summary>

```
land.py: --only-hunk verify tree includes the hunks it was told to leave out
```

</details>

<details><summary>after</summary>

```
land.py: the review lets --only-hunk read as narrowing the whole commit
```

</details>

<!-- relay:entry 20260924T223616Z-vq author=agent kind=rewrite model=glm-5.3 pane=2381c59f turn=8fb546ceb6f847e9b234227209058e6a/4f0e6e95ba3a4214a50555daced32954 -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
Unrelated fault noticed while landing #KDB4 (not reported by the user): `scripts/land.py commit --only-hunk <path>:<n> --confirm <digest>` materialises the verify tree from the *unselected* paths' working copies too, so the build gate checks a tree the commit would not put on main.

Measured, 2026-09-24, session `kdb4` landing #KDB4 (reproduced twice, including after `rm -rf /tmp/claude-1000/land/kdb4/verify`):

1. `begin kdb4 --base main src/Pane.h tests/test_conv_index.py` — the working tree's `src/Pane.h` held another session's uncommitted hunk (#VD2M, `relay::screen::rowHoldsPrompt`).
2. `commit kdb4 -m … --only-hunk tests/test_conv_index.py:1 --confirm 270315c68577` — held for review, digest printed, then the same command with `--confirm`.
3. The verify step materialised tree `3da90aac16be` whose `src/Pane.h:14832` **contains `rowHoldsPrompt`** — the excluded hunk — and the gate failed with `error: 'rowHoldsPrompt' is not a member of 'relay::screen'`, refusing a commit whose actual tree (tip + one Python test hunk) would not have contained it.

`--paths tests/test_conv_index.py --confirm <new digest>` landed the same change correctly (`dff42e06`), so the materialisation follows the path subset but not the hunk subset.

Two directions matter: a gate that fails a good commit wastes a verify cycle (this case), and a gate that builds a different tree than the one it lands can equally pass a commit whose real tree does not build — the exact incident class the gate exists for (`#04EC`/CLAUDE.md "what gets built is what would land").
```

</details>

<details><summary>after</summary>

```
*(Re-diagnosed 2026-09-24 18:40 by the filing session; the original text — kept in the thread — claimed the verify tree ignored `--only-hunk`. That mechanism was wrong.)*

`--only-hunk <path>:<n>` narrows **that one path's hunks**; every other claimed path lands whole. The tool's own output invites the wider reading, and a session (the filer's) followed it end to end:

1. Two paths claimed (`src/Pane.h`, which held another session's uncommitted #VD2M hunk, and `tests/test_conv_index.py`); the commit was held for review and digest `270315c68577` printed for **both** paths.
2. Re-ran with `--only-hunk tests/test_conv_index.py:1 --confirm 270315c68577` — the selection named the test path only, so `src/Pane.h` stayed wholly selected; plans, and therefore the digest, were unchanged, and `--confirm` matched.
3. The verify gate built tip + both paths — the tree the digest pins — and failed on the other session's half-hunk, refusing a commit that would have landed it.

The gate and the materialisation behaved as designed: the tree is built from the selected hunks and the digest covers the exact bytes of every path in it. What failed was communication. `land all of it: rerun **the same command** with --confirm` suggests the digest is tied to the command; `--only-hunk … (lands only those)` suggests it narrows the whole landing; and nothing in the review says that paths no selection names are still wholly in the commit.

The hazard is real in both directions: here the gate only wasted a verify cycle on a refused commit, but a session convinced `--only-hunk` had narrowed the tree can equally confirm a digest that lands a path it never reviewed.
```

</details>

<!-- relay:entry 20260924T223621Z-2j author=agent kind=note model=glm-5.3 pane=2381c59f turn=8fb546ceb6f847e9b234227209058e6a/4f0e6e95ba3a4214a50555daced32954 -->
Correction of the filing, by the filer: the original Issue text claimed the verify tree ignored `--only-hunk` ("materialises the verify tree from the unselected paths' working copies too"). Re-reading the code (`apply_hunks` feeds `plan_path`, whose merge is what `build_tree` writes out) and my own transcript shows the materialisation was right: `--only-hunk tests/test_conv_index.py:1` never said anything about `src/Pane.h`, so that path stayed wholly selected, the plans were identical, and the digest `270315c68577` legitimately matched. The gate refused a commit that would have landed the other session's half-hunk — it worked. The defect is the wording that invited the misreading; the Issue section now says so. The original text is preserved here in the thread.

<!-- relay:entry 20260924T223625Z-70 author=agent kind=event model=glm-5.3 pane=2381c59f turn=8fb546ceb6f847e9b234227209058e6a/4f0e6e95ba3a4214a50555daced32954 -->
- ✦ agent updated this card · appended to `## Done means`

<!-- relay:entry 20260924T223630Z-aj author=agent kind=event model=glm-5.3 pane=2381c59f turn=8fb546ceb6f847e9b234227209058e6a/4f0e6e95ba3a4214a50555daced32954 -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260924T223633Z-2r author=agent kind=event model=glm-5.3 pane=2381c59f turn=8fb546ceb6f847e9b234227209058e6a/4f0e6e95ba3a4214a50555daced32954 -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260924T223640Z-wp author=agent kind=event model=glm-5.3 pane=2381c59f turn=8fb546ceb6f847e9b234227209058e6a/4f0e6e95ba3a4214a50555daced32954 -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "code", "primary": "script", "also": ["probe"], "human": "none", "c…

<!-- relay:entry 20260924T223650Z-cg author=agent kind=event model=glm-5.3 pane=2381c59f turn=8fb546ceb6f847e9b234227209058e6a/4f0e6e95ba3a4214a50555daced32954 -->
- ✦ agent moved this card · Executing → Needs verification · Fix landed (622e7506); wording guard + regression tests, evidence in docs/qa_evidence/2026-09-24-land-only-hunk-scope/. · evidence docs/qa_evidence/2026-09-24-land-only-hunk-scope/ · implemented_by glm/glm-5.3
