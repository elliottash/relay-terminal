<!-- relay:entry 20260923T153400Z-g1 author=claude-code kind=evidence -->
### Claude Code · 2026-09-23 15:34 UTC
Filed under board rule 6 while landing bd48f2c4 (board writes per turn 20 → 100). The other 272 tests in test_board_tools and test_guest_board_bridge pass; this one fails identically on a clean export of HEAD. Not fixed here.

<!-- relay:entry 20260923T180741Z-8b author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260923T180742Z-wj author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260923T181725Z-3c author=agent kind=event model=kimi-k3 pane=switchboard turn=477c6c37a829976c4216d8ec372ae0a1/54c71a17be864010bf95a7f49f2c739a -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260923T181808Z-t4 author=agent kind=event model=kimi-k3 pane=switchboard turn=477c6c37a829976c4216d8ec372ae0a1/54c71a17be864010bf95a7f49f2c739a -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260923T181821Z-0q author=agent kind=comment mode=plan model=kimi-k3 turn=477c6c37a829976c4216d8ec372ae0a1/54c71a17be864010bf95a7f49f2c739a -->
I'll read the card and the bridge code it points at.







Now I need to see how a guest/provider turn runs and what happens to board writes when the turn raises — `board_tools.py`, `agent.py`, and the provider harness path.













Let me read the FakeHarness to see how the scripted turn runs and what an exception inside it becomes.







Let me pin down the write gates in `BoardTools.run` and the `_comment` path — the refusal texts around lines 1692–1725 and 2616+.



Very promising lead: the scripted turn's first line asserts `'relay_board' in prompt`. Let me check where the board policy reaches a guest — `agent.py` and `guest_instructions.py`.





One more check — how the Agent's system prompt gets the board policy, and whether any of it reaches a guest's send prompt.





Now the plan itself.

**Done:** #GBN4 has a revised `## Done means` and a `## Plan`. The plan's key finding: the comment is never written because the test's scripted turn raises before the `board_comment` call — most likely at its first line, `assertIn('relay_board', prompt)`, since `HarnessProvider.complete()` now sends only the user text while the instructions that mention `relay_board` go to `harness.start()`. The write-durability contract itself (keep the write, stamp it, revoke after failure) is already how BoardTools and the bridge work, so no rollback needs removing.

One thing for the executor to settle first: whether the #MEMS commits (28e74f5e, f4cd1def) dropped the instructions from a fresh guest's first prompt (→ restore it in `complete()`) or whether instructions-at-start was always the design (→ fix the test's first assertion). If the git history doesn't settle it, the plan flags it as an owner question.

<!-- relay:entry 20260923T181821Z-0r author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
