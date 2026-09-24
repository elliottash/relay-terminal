<!-- relay:entry 20260921T190025Z-c4 author=agent kind=event model=kimi-k3 pane=bcb3f818 turn=c9a24a6d1b3e4f47b3325a66b47a1b74/c97deb0e4a3845689d17d758550ff51b -->
- ✦ agent created this card in Inbox · issues/changes/2026-09-21-pane-flips-between-glm-5-3-and-astra-while-in-pl.md

<!-- relay:entry 20260921T190425Z-6q author=agent kind=event model=kimi-k3 pane=bcb3f818 turn=c9a24a6d1b3e4f47b3325a66b47a1b74/c97deb0e4a3845689d17d758550ff51b -->
- ✦ agent claimed this card · assignee agent, Inbox → Executing, implemented_by kimi/kimi-k3, session bcb3f818

<!-- relay:entry 20260921T190425Z-6r author=agent kind=progress model=kimi-k3 pane=bcb3f818 turn=c9a24a6d1b3e4f47b3325a66b47a1b74/c97deb0e4a3845689d17d758550ff51b pane_token=bcb3f818-cb66-4d82-935e-70c05a46c339 -->
Claimed (bcb3f818) · working on it from a terminal pane

Root cause traced from worker.log for session bde50672: every plan-mode turn fires plan_route astra → glm-5.3 (effort=max, source=configured) and back at turn end — 9 plan turns, 18 visible picker flips. Cause: the planning role's default follows the High tier (roles.py), which resolves to a different model than the pane's. #Z0VG's design says the default should be the pane's own model at max effort, a hand-picked planning model wins. Fixing the default in the backend + regression test.

<!-- relay:entry 20260921T192111Z-0s author=agent kind=decision model=kimi-k3 pane=bcb3f818 turn=c9a24a6d1b3e4f47b3325a66b47a1b74/c97deb0e4a3845689d17d758550ff51b -->
Owner, asked what a plan-mode turn in a Codex/astra pane should run on: "it should run on codex astra in xhigh". So the planning role's default goes back to #Z0VG's design — the pane's own model pushed to max effort — and the 2026-09-20 mapping of planning onto the High tier is reverted: only a hand-pinned `roles.planning` entry routes a plan turn to another model. The astra→glm-5.3→astra flip per plan turn then never happens unless the owner pins one.

<!-- relay:entry 20260921T195631Z-bj author=agent kind=event model=kimi-k3 pane=bcb3f818 turn=c9a24a6d1b3e4f47b3325a66b47a1b74/c97deb0e4a3845689d17d758550ff51b -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"plans": [], "commits": ["08cd7f37", "7f9b59e4"], "evidence": [], "related": ["…; appended to `## Execution Summary`

<!-- relay:entry 20260921T195651Z-75 author=agent kind=event model=kimi-k3 pane=bcb3f818 turn=c9a24a6d1b3e4f47b3325a66b47a1b74/c97deb0e4a3845689d17d758550ff51b -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260921T195714Z-q2 author=agent kind=event model=kimi-k3 pane=bcb3f818 turn=c9a24a6d1b3e4f47b3325a66b47a1b74/c97deb0e4a3845689d17d758550ff51b -->
- ✦ agent moved this card · Executing → Done, session bcb3f818 released · Fixed in 08cd7f37 + 7f9b59e4: planning's default is the pane's own model at max (a Codex pane plans on astra at xhigh via the new guest boost); only a hand pin routes a plan turn off it. Owner decision made inline; 285 targeted tests green except three foreign/pre-existing failures (#GPF7, another session's ranking churn). · verified_by kimi/kimi-k3
