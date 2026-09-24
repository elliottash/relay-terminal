<!-- relay:entry 20260921T185943Z-01 author=codex kind=evidence -->
### Codex · 2026-09-21 18:59
Filed the independently reproduced baseline assertion failure; no unrelated fix included in #XP7N.

<!-- relay:entry 20260921T194633Z-gp author=agent kind=evidence model=kimi-k3 pane=bcb3f818 turn=c9a24a6d1b3e4f47b3325a66b47a1b74/c97deb0e4a3845689d17d758550ff51b -->
Reproduced again on 2026-09-21 while landing #HR5E: `tests.test_plan_turns.GuestPlanTurnTests.test_a_plan_turn_on_a_glm_pane_runs_through_codex_and_comes_back` fails at `assertTrue(sent.startswith("PLAN MODE."))` — the sent prompt actually begins with the relay_board MCP preamble ("Relay offers an MCP server named relay_board…"). Confirmed independent of the #HR5E changes (they touch role resolution and the plan-turn route, not guest prompt construction).

<!-- relay:entry 20260921T194633Z-zg author=agent kind=event model=kimi-k3 pane=bcb3f818 turn=c9a24a6d1b3e4f47b3325a66b47a1b74/c97deb0e4a3845689d17d758550ff51b -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started

<!-- relay:entry 20260922T042733Z-d2 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260922T042733Z-q0 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Discussing → Planning · a Plan turn started

<!-- relay:entry 20260922T043002Z-v3 author=agent kind=event model=kimi-k3 pane=switchboard turn=c070fc99379452cd5f28ed08fe56e3b7/e07cb0420f0f4d6bb77548f0e126e18d -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260922T043032Z-pr author=agent kind=event model=kimi-k3 pane=switchboard turn=c070fc99379452cd5f28ed08fe56e3b7/e07cb0420f0f4d6bb77548f0e126e18d -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260922T043040Z-7p author=agent kind=comment mode=plan model=kimi-k3 turn=c070fc99379452cd5f28ed08fe56e3b7/e07cb0420f0f4d6bb77548f0e126e18d -->
Reading #GPF7 and the test it names, plus the guest-prompt construction code.





The test already carries the fixed assertion. Now finding where the guest preamble gets prepended to the prompt.



Now reading the plan-turn route in `agent.py` to see how the guest provider is started for a plan turn.









I have the full picture now. Writing `## Done means` first.



**Done:** #GPF7 is planned. The key finding: the working tree's `tests/test_plan_turns.py` already carries the tolerant assertions (`assertIn` + `assertLess`, tagged `#GPF7`), and the prefix comes from `HarnessProvider.complete` prepending the relay_board MCP preamble after applying the plan-turn opening — so the plan is mostly verify, land, and close.

One open question is parked in the plan's Risks, not blocking: whether a one-turn read-only planning guest should get the board-delegation preamble at all — that's a code change for a separate card if you want it.

<!-- relay:entry 20260922T043040Z-7q author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260923T164251Z-cx author=agent kind=evidence model=gpt-6-sol pane=2e7406f6 turn=1146f84a5b7d4390a2b9887b7a77b357/5adddc8c89644a4c81da1d5935583642 -->
2026-09-23 related prompt-contract failure while verifying #XP7N: `PYTHONPATH=backend:tests python3 -m unittest tests.test_guest_board_bridge tests.test_plan_turns tests.test_sessions.PlanModeTests -q` ran 58 tests with one failure, `BridgeTests.test_provider_turn_binds_native_context_and_revokes_on_failure`. It asserts `relay_board` is in the guest's turn prompt, but the fake harness received `comment on the card`; Relay supplies bridge instructions through `harness.instructions`. This is the same stale prompt-placement assumption as #GPF7 in a second test. The focused 28 Plan tests pass.

<!-- relay:entry 20260924T042245Z-ex author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260924T042245Z-zg author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planned → Planning · a Plan turn started

<!-- relay:entry 20260924T042338Z-eq author=agent kind=event model=kimi-k3 pane=switchboard turn=afa5bb29a240ddc600ac3bb56c668a08/2e8b7cb242904840b2aa2c79e5658c2a -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260924T042406Z-zd author=agent kind=event model=kimi-k3 pane=switchboard turn=afa5bb29a240ddc600ac3bb56c668a08/2e8b7cb242904840b2aa2c79e5658c2a -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260924T042411Z-pb author=agent kind=comment mode=plan model=kimi-k3 turn=afa5bb29a240ddc600ac3bb56c668a08/2e8b7cb242904840b2aa2c79e5658c2a -->
Both tests named in the thread already carry the corrected assertions in the working tree — the bridge test now asserts on `harness.instructions` (line 280) and the plan test on order, not position 0. Revising `## Done means` first to cover both, then the plan.



**Done:** #GPF7's plan is revised to cover both stale prompt-placement assumptions — the plan-turn `PLAN MODE.` prefix and, from the 2026-09-23 evidence, the board-bridge test's prompt-text assertion (now correctly on `harness.instructions`). Both tests already carry the tolerant form in the working tree, so the plan is mostly verify, land any uncommitted edits, and close. Nothing is needed from you; the open question about suppressing the MCP preamble for plan guests stays parked in Risks for a separate card if you want it.

<!-- relay:entry 20260924T042412Z-t1 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260924T042827Z-e3 author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Planned → Executing, session 0c551c5d

<!-- relay:entry 20260924T042827Z-w9 author=owner kind=progress pane=switchboard pane_token=0c551c5d-2755-4d8b-af79-b52539aab4d5 -->
Claimed (0c551c5d) · working on it from a terminal pane

Run pressed on iOS Safari.

<!-- relay:entry 20260924T044051Z-wa author=agent kind=event model=kimi-k3 pane=0c551c5d turn=c7db67b8fe0e49128b51923a985e73c6/ca0c651b04be4866b50061f9089960fd -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["XP7N"], "github": null… → {"plans": [], "commits": ["347678b7", "5904a0e0"], "evidence": ["docs/qa_evidenc…; replaced `## Execution Summary`

<!-- relay:entry 20260924T044103Z-hw author=agent kind=progress model=kimi-k3 pane=0c551c5d turn=c7db67b8fe0e49128b51923a985e73c6/ca0c651b04be4866b50061f9089960fd -->
Plan carried out. The card was not verify-and-close after all: the named test failed one line earlier than the Issue said because #RND7's failover change (6063dff7) made `_begin_plan_turn`'s guest-start `while` loop re-enter after a successful start — the harness was started twice, the second start failed, and the turn planned without its guest. Fixed with a break on success (347678b7). The stale test_sessions prefix assertion found in step 4 is #JKG2 (f7df0d36). Both named tests pass; 112 tests across test_plan_turns, test_guest_board_bridge and test_sessions OK offline. Evidence in docs/qa_evidence/2026-09-24-gpf7-plan-guest-double-start/. Moving to needs-verification.

<!-- relay:entry 20260924T044116Z-mq author=agent kind=event model=kimi-k3 pane=0c551c5d turn=c7db67b8fe0e49128b51923a985e73c6/ca0c651b04be4866b50061f9089960fd -->
- ✦ agent moved this card · Executing → Needs verification · Guest double-start fixed (347678b7), both named tests and all three modules green offline; evidence and ## Tests on the card. · evidence docs/qa_evidence/2026-09-24-gpf7-plan-guest-double-start/ · implemented_by kimi/kimi-k3
