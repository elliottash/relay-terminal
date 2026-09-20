<!-- relay:entry 20260920T031644Z-z0 author=agent kind=event model=glm-5.3 pane=6a4eefda turn=f4c62ab8e34947aebe8d463e3838fe67/f37d755e3c1e49af81f48729cf670477 -->
- ✦ agent created this card in Inbox · issues/changes/2026-09-19-card-id-bvl1-is-not-crockford-base32-check-alway.md

<!-- relay:entry 20260920T032433Z-k1 author=agent kind=event model=claude-opus-5 -->
- ✦ agent moved this card · Inbox → Done · Renamed to #BVK1 everywhere the board reads it; `relay-board.py check` is clean

<!-- relay:entry 20260920T032433Z-k2 author=agent kind=progress model=claude-opus-5 -->
The bevel card's id is now **#BVK1** (K in place of the excluded L, so it still reads as the same card and BVL1 stays recognisable in history). Changed in the card's `id:` field, in `links.related` and the body reference of the pane-buttons card, and in that one row of `issues/BOARD.md` — edited by hand rather than regenerated with `relay-board.py index`, because several sessions have uncommitted card edits in this tree and a regenerated index would have committed theirs along with mine.

`python3 scripts/relay-board.py check`: 306 cards, **0 errors**, down from 1. The remaining `task_missing_marker` warning on the pane-buttons card is pre-existing and not part of this.

The three evidence and thread files that mention BVL1 (`docs/qa_evidence/2026-09-18-pane-buttons-brighter-outline/README.md`, `docs/qa_evidence/2026-09-20-switchboard-sections/README.md`, `issues/threads/3XZV.md`) are left as they are: they record what was true when they were written.

<!-- relay:entry 20260920T032444Z-k3 author=agent kind=event model=claude-opus-5 -->
- ✦ agent updated this card · links.commits: [] → ["02d65e5f"]
