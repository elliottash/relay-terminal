<!-- relay:entry 20260921T185943Z-01 author=codex kind=evidence -->
### Codex · 2026-09-21 18:59
Filed the independently reproduced baseline assertion failure; no unrelated fix included in #XP7N.

<!-- relay:entry 20260921T194633Z-gp author=agent kind=evidence model=kimi-k3 pane=bcb3f818 turn=c9a24a6d1b3e4f47b3325a66b47a1b74/c97deb0e4a3845689d17d758550ff51b -->
Reproduced again on 2026-09-21 while landing #HR5E: `tests.test_plan_turns.GuestPlanTurnTests.test_a_plan_turn_on_a_glm_pane_runs_through_codex_and_comes_back` fails at `assertTrue(sent.startswith("PLAN MODE."))` — the sent prompt actually begins with the relay_board MCP preamble ("Relay offers an MCP server named relay_board…"). Confirmed independent of the #HR5E changes (they touch role resolution and the plan-turn route, not guest prompt construction).

<!-- relay:entry 20260921T194633Z-zg author=agent kind=event model=kimi-k3 pane=bcb3f818 turn=c9a24a6d1b3e4f47b3325a66b47a1b74/c97deb0e4a3845689d17d758550ff51b -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started
