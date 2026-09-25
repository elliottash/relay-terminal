<!-- relay:entry 20260923T230251Z-n7 author=agent kind=event model=gpt-6-sol pane=0797aebe turn=bf07ea53fe1e47b383aeb5e7025ce0da/f81c5675547c49e8b9e2a23d8e2471fe -->
- ✦ agent created this card in Planned · issues/features/2026-09-23-make-board-the-default-and-migrate-existing-loca.md

<!-- relay:entry 20260923T230254Z-g2 author=agent kind=event model=gpt-6-sol pane=0797aebe turn=bf07ea53fe1e47b383aeb5e7025ce0da/f81c5675547c49e8b9e2a23d8e2471fe -->
- ✦ agent claimed this card · assignee agent, Planned → Executing, implemented_by openai/gpt-6-sol via codex, session 0797aebe

<!-- relay:entry 20260923T230254Z-x4 author=agent kind=progress model=gpt-6-sol pane=0797aebe turn=bf07ea53fe1e47b383aeb5e7025ce0da/f81c5675547c49e8b9e2a23d8e2471fe pane_token=0797aebe-a24a-44cf-8274-75768ab2cf94 -->
Claimed (0797aebe) · working on it from a terminal pane

Update default board discovery and setup, migrate other local project boards, then migrate this repository's board last.

<!-- relay:entry 20260923T230334Z-nz author=agent kind=event model=gpt-6-sol pane=0797aebe turn=bf07ea53fe1e47b383aeb5e7025ce0da/f81c5675547c49e8b9e2a23d8e2471fe -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260923T230346Z-p6 author=agent kind=event model=gpt-6-sol pane=0797aebe turn=bf07ea53fe1e47b383aeb5e7025ce0da/f81c5675547c49e8b9e2a23d8e2471fe -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260923T231543Z-78 author=agent kind=event model=gpt-6-sol pane=0797aebe turn=bf07ea53fe1e47b383aeb5e7025ce0da/f81c5675547c49e8b9e2a23d8e2471fe -->
- ✦ agent updated this card · tasks: 3/5 done

<!-- relay:entry 20260923T231611Z-dm author=agent kind=event model=gpt-6-sol pane=0797aebe turn=bf07ea53fe1e47b383aeb5e7025ce0da/f81c5675547c49e8b9e2a23d8e2471fe -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["1CXD"], "github": null… → {"plans": [], "commits": ["9bd89b7cfc21e81576a5e38e61c8fce3e2841908"], "evidence…

<!-- relay:entry 20260923T232128Z-e2 author=owner kind=event pane=0797aebe -->
Released (0797aebe) · the pane closed

<!-- relay:entry 20260924T202455Z-v1 author=agent kind=event model=kimi-k3 pane=297817d3 turn=bf07ea53fe1e47b383aeb5e7025ce0da/1ac2797b35d24916a2b91df81954d9e7 -->
- ✦ agent claimed this card · implemented_by kimi/kimi-k3, session 297817d3

<!-- relay:entry 20260924T202455Z-v2 author=agent kind=progress model=kimi-k3 pane=297817d3 turn=bf07ea53fe1e47b383aeb5e7025ce0da/1ac2797b35d24916a2b91df81954d9e7 pane_token=297817d3-877f-4125-a584-deb56f03d1b5 -->
Claimed (297817d3) · working on it from a terminal pane

Final step: commit the issues/ -> .board/ rename in this repository, regenerate policy/pointers, restore the compatibility link.

<!-- relay:entry 20260924T204350Z-xk author=agent kind=evidence model=kimi-k3 pane=297817d3 turn=bf07ea53fe1e47b383aeb5e7025ce0da/1ac2797b35d24916a2b91df81954d9e7 -->
Rename landed as 31e513a2c59b (1,274 files changed: 1,158 issues/ deletions + 1,208 .board/ additions + regenerated policy/pointers, .gitattributes, WARP.md, scripts/land.py intake paths, two board briefs). Verified after landing: `git ls-files issues` = 0, `git ls-files .board` = 1,208; `issues -> .board` compatibility link restored (untracked); `scripts/relay-board.py check` runs against .board: 684 cards, 2 errors (both pre-existing/other-session content faults, filed as #43XK), 1,281 content warnings unchanged in nature; zero files missing vs the pre-move backup at /home/elliott/data/relay-checkout-archive/2026-09-23-GRT2-pre-restart/. Landed through land.py with the link lowered during the commit so issues/… resolved as deletions; --base main snapshots avoided modify/delete conflicts with ~120 board files carrying other sessions' uncommitted edits (those edits are preserved in the .board/ additions). Evidence: docs/qa_evidence/2026-09-23-board-migration-GRT2/README.md.

<!-- relay:entry 20260924T204503Z-31 author=agent kind=event model=kimi-k3 pane=297817d3 turn=bf07ea53fe1e47b383aeb5e7025ce0da/1ac2797b35d24916a2b91df81954d9e7 -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "code", "primary": "script", "also": [], "human": "optional", "crit…; tasks: 5/5 done

<!-- relay:entry 20260924T204515Z-k2 author=agent kind=event model=kimi-k3 pane=297817d3 turn=bf07ea53fe1e47b383aeb5e7025ce0da/1ac2797b35d24916a2b91df81954d9e7 -->
- ✦ agent moved this card · Executing → Needs verification · All five tasks done: .board is the default for new boards, the four other local project boards migrated, and this repository's issues/ board renamed in commit 31e513a2 with the policy, pointers, gitattributes and script references updated. Evidence in docs/qa_evidence/2026-09-23-board-migration-GRT2/. · evidence docs/qa_evidence/2026-09-23-board-migration-GRT2/ · implemented_by kimi/kimi-k3
