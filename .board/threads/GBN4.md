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

<!-- relay:entry 20260924T042835Z-z5 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260924T042835Z-z6 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planned → Planning · a Plan turn started

<!-- relay:entry 20260924T043034Z-0p author=agent kind=event model=kimi-k3 pane=switchboard turn=dcbd706ec49ab0b9047262d78d6cc723/0a5798ff19db4e32b4077b029c82a189 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260924T043107Z-42 author=agent kind=event model=kimi-k3 pane=switchboard turn=dcbd706ec49ab0b9047262d78d6cc723/0a5798ff19db4e32b4077b029c82a189 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260924T043115Z-34 author=agent kind=comment mode=plan model=kimi-k3 turn=dcbd706ec49ab0b9047262d78d6cc723/0a5798ff19db4e32b4077b029c82a189 -->
I'll re-read the card's key code paths to see whether the plan's open question (instructions in the first prompt vs. at `start()`) can be settled from the current tree.











The plan's open question is settled, so I'll revise `## Done means` first, then the plan.



**Done:** #GBN4's `## Done means` and `## Plan` are revised. The big change: the first plan's open question is already settled — the test now asserts `'relay_board' in harness.instructions` (marked #GPF7), and the instructions genuinely reach `harness.start()`, so the plan is now "run the test on current main first; it may already pass, and only then dig into the bridge dispatch." Nothing is needed from the owner; the card is ready for a Run.

<!-- relay:entry 20260924T043116Z-hc author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260924T204000Z-x3 author=agent kind=event model=kimi-k3 pane=e6f7dd8d turn=198f044065dd47079fe9509e50f12651/3f827c9465694ac589d554e838521647 -->
- ✦ agent claimed this card · assignee agent, Planned → Executing, implemented_by kimi/kimi-k3, verify defaulted from skill deliver, session e6f7dd8d

<!-- relay:entry 20260924T204000Z-x4 author=agent kind=progress model=kimi-k3 pane=e6f7dd8d turn=198f044065dd47079fe9509e50f12651/3f827c9465694ac589d554e838521647 pane_token=e6f7dd8d-deb3-4a1a-a2d9-44b3577064f1 -->
Claimed (e6f7dd8d) · working on it from a terminal pane

Verifying whether the four scenarios now pass since #GPF7 landed; fixing the exception path only if they still fail.

<!-- relay:entry 20260924T204015Z-qb author=agent kind=progress model=kimi-k3 pane=e6f7dd8d turn=198f044065dd47079fe9509e50f12651/3f827c9465694ac589d554e838521647 -->
Paused on owner request before running any code. No changes made yet; card stays claimed by this pane pending resume.
