---
id: AVQH
type: work
status: executing
labels: [bug, privacy, board]
assignee: agent
priority: 2
rank: zzzzzzzzzzzzzzzzzzzzr
created: '2026-09-25'
verify: {artifact: system, primary: script, also: [person], human: required, criteria: 'A pattern scan of `git log -p 18bd63bb^..main` over the rewritten history finds none of the personal strings (collaborator name/email, the two tracker URLs, ~/orgus, judge-gpt); the tip tree sha is byte-identical before and after; origin/main never receives a pre-rewrite commit.', sign_off: none, effort: medium, stakes: reputation}
source: pane 1, 2026-09-25
links: {plans: [], commits: [339aebe5], evidence: [docs/qa_evidence/2026-09-25-avqh-history-scrub/], related: [V3R3], github: null}
---
# Remove personal tracker data from relay-terminal history before main is pushed

## Issue
Commit 18bd63bb and five after it wrote personal details into seven public-repo files; the current tree is redacted but local history still holds them, and any push of main would publish them. The remote has not received the commits. The rewrite plan: scrub the six commits in a scratch clone, verify, swap main by update-ref with an identical tip tree.

> file a card with your plan for removing private info from the historical commits. then lets get back to V3R3
> — elliott · [session:f849e98a61a842289f0e84f9fb07fba1](relay://session/f849e98a61a842289f0e84f9fb07fba1) · 2026-09-25

## Done means
The rewritten `main` contains none of the personal strings anywhere in `18bd63bb^..main`; the tip tree is byte-identical before and after the rewrite (same tree sha), so the shared working tree and index are untouched; origin (github.com/elliottash/relay-terminal) never receives a pre-rewrite commit; relay-internal is left alone (private by design, the ground-truth file lives there intentionally).

## Tasks

- [x] Freeze pushes: note on the board and in land.py `who` contact that no session pushes main until this card is done <!-- t:hz -->
- [x] Re-verify exposure: origin/main is still an ancestor older than 18bd63bb; no branch, tag, stash or other remote holds it <!-- t:dd -->
- [x] Enumerate the offending strings and blobs in the six commits; build the filter-repo replace-text file <!-- t:a7 -->
- [x] Rewrite 18bd63bb^..main in a scratch clone with git filter-repo (install it), fallback: filter-branch in the clone <!-- t:cx -->
- [x] Verify in the clone: pattern scan of git log -p is clean; tip tree sha unchanged; commit count unchanged; relay-board.py check clean on a clean export <!-- t:mc -->
- [x] Swap the checkout's main with git update-ref (no working-tree write); run scripts/land.py doctor <!-- t:vc -->
- [ ] Expire main's reflog and gc in checkout and clone; drop the clone; land.py gc for /tmp snapshots holding old file text <!-- t:rx s=in-progress -->
- [ ] Unfreeze; owner pushes main at the next normal push <!-- t:zf -->

## Plan
## Scope (verified 2026-09-25)

Six commits on local `main` carry the data: `18bd63bb`, `6c334cd3` (board sync re-included some of it), `74b72761`, `c28693de`, `344f3029`, `6eb33206`, across `docs/GLOBAL-PROJECT-BOARD-RESEARCH.md`, `docs/research/global-project-board/` (3 files), `docs/README.md`, `.board/design/2026-09-24-a-global-project-board-above-the-per-project-boa.md`, `.board/threads/V3R3.md`. The current tree is clean (pattern scan finds 0 hits); `git log -p` over those paths finds 60. Only `refs/heads/main` holds them — no branches, tags, stash or other remote. `origin/main` = `9a5d57b1`, an ancestor of `18bd63bb^`: never pushed. `git-filter-repo` is not installed.

## Steps

1. **Freeze.** Post the freeze on the board and set a land.py contact so live sessions know not to push. Nothing in land.py pushes, so the risk is a manual or release push.
2. **Rewrite in a scratch clone, never in the checkout** (CLAUDE.md forbids checkout/reset here; the working tree holds other sessions' uncommitted work). `git clone --no-hardlinks` to a scratch dir, then `git filter-repo --refs 18bd63bb^..main --replace-text patterns.txt` scrubbing: the collaborator name and email, the Trello board id/URL, the Sheet id/URL, `~/orgus`, `judge-gpt`, and the named project inventory. Install git-filter-repo into the scratch dir with pip `--target`; fallback: `git filter-branch --tree-filter` over the same range. Commit messages carry no personal strings (verify).
3. **Verify in the clone before touching anything:** pattern scan over `git log -p 18bd63bb^..main` is clean; the new tip's **tree sha equals `2f6695e8…`** (tip content is already redacted, so only history changes); commit count over the range is unchanged; `relay-board.py check` on a clean export of the new tip reports no new errors.
4. **Swap without writing the tree.** `git update-ref refs/heads/main <new> <old>` from the checkout. Because the tree is identical, the index and working tree stay valid; run `scripts/land.py doctor` to confirm. Sessions with in-flight land.py commits will fail one compare-and-swap against the old sha and retry against the new tip — that path is already handled by the tool.
5. **Purge local remnants.** `git reflog expire --expire=now main && git gc --prune=now` in the checkout and the clone, drop the clone, and run `land.py gc` so `/tmp/claude-1000/land/*/snap` snapshots holding the old file text go away.
6. **Unfreeze**, then the owner pushes at the next normal push.

## Risks
- A session lands a new commit mid-rewrite: recompute the rewrite onto the new tip (the range end moves; the six bad commits themselves do not change).
- Someone pushes during the window: mitigated by the freeze note; verify `origin/main` again at step 2 and step 6.
- ~~filter-repo availability~~ — resolved 2026-09-25: owner approved installing git-filter-repo.
- Rewriting loses the original shas: acceptable, nothing remote references them.

## Decisions
- **2026-09-25 — git-filter-repo install approved.** Owner: "you can install git-filter-repo". Install it normally (pip), no filter-branch fallback needed except as dead contingency.

## Execution Summary
2026-09-25. Rewrote `18bd63bb^..main` (173 commits, tip `6f66ca9c`) in a scratch clone with `git filter-repo --file-info-callback`, scoped to the seven #V3R3 paths only. Plain `--replace-text` would have run over the whole repository, and other tip files legitimately contain `orgus`/`alfred`, so it would have changed the tip tree. Swapped with `git update-ref refs/heads/main 5b9ce8cd 6f66ca9c` (compare-and-swap). The tip tree `c976a587` is identical, the index and working tree were not touched, and `land.py doctor` is clean. Pattern hits in `git log -p` over the range went from 54 to 0; commit count 173 to 173; metadata identical. The only difference is 9 subjects whose cited shas filter-repo translated to the new ones. `origin/main` = `9a5d57b1` throughout. The scan patterns are kept privately in `relay-internal/planning/avqh-history-scrub-patterns.txt` (relay-internal `12c60e1`), and the scratch clone and extracted blobs are deleted. No land.py snapshot or verify slot held the strings. The two deviations from the plan are listed below.

- **Deferred prune.** The first `gc --prune=2.hours.ago` kept the old objects: the old rewritten commits up to 12:58 are recent unreachable objects, and git keeps everything they reach. `--prune=now` could delete objects that another session's in-flight `land.py commit` has written but not yet referenced, so a background job runs `reflog expire --expire-unreachable=now` and `gc --prune=2.hours.ago` at 15:05 local and checks that the old objects are gone.
- **Old shas in cards.** 77 files under `.board/` and `docs/` cite pre-rewrite shas that the unchanged tip tree cannot update. `docs/qa_evidence/2026-09-25-avqh-history-scrub/commit-map.tsv` maps old to new.

## Tests
- Pattern scan (evidence README, "Rerun the scan"): `git log -p 95d6f6ed^..main | grep -cE -f <private patterns>` returns 0; over the seven paths with bare `orgus`/`alfred` added, it also returns 0.
- `git rev-parse main^{tree}` before and after the swap: `c976a587` both times.
- `git ls-remote origin refs/heads/main` returns `9a5d57b1`.
- `git cat-file -e` on the six old commits and the old blobs fails once the 15:05 prune has run (pending).
