# #AVQH: history scrub of personal tracker data (2026-09-25)

Commit `18bd63bb` (#V3R3) and five after it wrote personal tracker details into seven files. The
tip was already redacted; this rewrote history so no commit holds them. `origin/main` was
`9a5d57b1` throughout (checked with `git ls-remote` before and after), an ancestor of the range, so
nothing was ever published.

## What was done

- Scratch clone of `main` at `6f66ca9c` (tree `c976a587`), `git filter-repo --refs
  18bd63bb^..main --file-info-callback`, rewriting blob text **only** under the seven #V3R3 paths
  (`docs/GLOBAL-PROJECT-BOARD-RESEARCH.md`, `docs/research/global-project-board/*`,
  `docs/README.md`, the V3R3 design card, `.board/threads/V3R3.md`). Replace-text over the whole
  repository was rejected: other tip files legitimately mention some of the same words, and the
  tip tree had to stay byte-identical.
- Replaced: the collaborator's name and email, the Trello board URL and id, the spreadsheet URL
  and id, the example project's name/slug/card id/label/summary, `~/orgus`, and the credential
  holder's name. The pattern list is private:
  `relay-internal/planning/avqh-history-scrub-patterns.txt`.
- Swap: `git update-ref refs/heads/main 5b9ce8cd 6f66ca9c` (compare-and-swap, no working-tree or
  index write). `scripts/land.py doctor` clean afterwards.

## Checks

| Check | Before | After |
|---|---|---|
| tip tree | `c976a587` | `c976a587` |
| commits in range | 173 | 173 |
| pattern hits, `git log -p <range>` (all paths) | 54 | 0 |
| hits over the seven paths, bare `orgus`/`alfred` included | — | 0 |
| author, email, author/committer dates, subjects | — | identical except 9 subjects whose cited shas filter-repo translated to the new ones |

The range now starts at `95d6f6ed` ("Global project board research and proposal (#V3R3)").

## Old shas in cards

The tip tree is unchanged, so cards and threads still cite the pre-rewrite shas of these 173
commits (77 files under `.board/` and `docs/` hold at least one). `commit-map.tsv` maps each old
sha to its new one (tab-separated, oldest first); `git show <new>` is the same change.

## Rerun the scan

```
B=$(git log --format=%H --grep='Global project board research and proposal (#V3R3)' main)
grep -v '^#' ~/repos/relay-internal/planning/avqh-history-scrub-patterns.txt > /tmp/p.txt
git log -p $B^..main | grep -cE -f /tmp/p.txt        # expect 0
```
