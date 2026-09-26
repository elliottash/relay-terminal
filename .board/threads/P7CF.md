<!-- relay:entry 20260925T001219Z-r8 author=agent kind=event model=gpt-6-sol pane=3e309d9d turn=fc8daae437874e24b11a016cd98ce58c/18198267bb14475c9a59e60a1a807e5d -->
- ✦ agent created this card in Planned · .board/design/2026-09-24-risk-based-verification-and-backlog-closure.md

<!-- relay:entry 20260925T001223Z-nc author=agent kind=event model=gpt-6-sol pane=3e309d9d turn=fc8daae437874e24b11a016cd98ce58c/18198267bb14475c9a59e60a1a807e5d -->
- ✦ agent claimed this card · assignee agent, Planned → Executing, implemented_by openai/gpt-6-sol via codex, session 3e309d9d

<!-- relay:entry 20260925T001223Z-nd author=agent kind=progress model=gpt-6-sol pane=3e309d9d turn=fc8daae437874e24b11a016cd98ce58c/18198267bb14475c9a59e60a1a807e5d pane_token=3e309d9d-aa32-4dd4-988d-c2105e1b84a9 -->
Claimed (3e309d9d) · working on it from a terminal pane

Research current AI-heavy development verification practices, measure this Board's backlog, and write a risk-based closure proposal without bulk-closing cards.

<!-- relay:entry 20260925T001236Z-4g author=agent kind=event model=gpt-6-sol pane=3e309d9d turn=fc8daae437874e24b11a016cd98ce58c/18198267bb14475c9a59e60a1a807e5d -->
- ✦ agent updated this card · verify: (unset) → {"artifact": "text", "primary": "person", "also": [], "human": "optional", "crit…; appended to `## Plan`; replaced `## Done means`

<!-- relay:entry 20260925T002313Z-gp author=agent kind=event model=gpt-6-sol pane=3e309d9d turn=fc8daae437874e24b11a016cd98ce58c/18198267bb14475c9a59e60a1a807e5d -->
- ✦ agent updated this card · appended to `## Planning notes`

<!-- relay:entry 20260925T002323Z-nk author=agent kind=event model=gpt-6-sol pane=3e309d9d turn=fc8daae437874e24b11a016cd98ce58c/18198267bb14475c9a59e60a1a807e5d -->
- ✦ agent updated this card · appended to `## Tests`; tasks: 3/3 done

<!-- relay:entry 20260925T002335Z-gj author=agent kind=question model=gpt-6-sol pane=3e309d9d turn=fc8daae437874e24b11a016cd98ce58c/18198267bb14475c9a59e60a1a807e5d -->
1. Recommendation: retain human sign-off for consequential or subjective work, recognize your explicit chat instruction as approval to close a named card, and pilot evidence-gated closure for low-risk cards before changing the default. Should this be the policy direction for implementation?

<!-- relay:entry 20260925T002339Z-63 author=agent kind=event model=gpt-6-sol pane=3e309d9d turn=fc8daae437874e24b11a016cd98ce58c/18198267bb14475c9a59e60a1a807e5d -->
- ✦ agent updated this card · waiting_on: (unset) → owner

<!-- relay:entry 20260925T002343Z-8d author=agent kind=event model=gpt-6-sol pane=3e309d9d turn=fc8daae437874e24b11a016cd98ce58c/18198267bb14475c9a59e60a1a807e5d -->
- ✦ agent moved this card · Executing → Discussing · Research and backlog audit are recorded; the policy direction is ready for the owner's decision.

<!-- relay:entry 20260925T002413Z-nm author=agent kind=event model=gpt-6-sol pane=3e309d9d turn=fc8daae437874e24b11a016cd98ce58c/18198267bb14475c9a59e60a1a807e5d -->
- ✦ agent updated this card · replaced `## Planning notes`

<!-- relay:entry 20260925T002449Z-vf author=agent kind=event model=gpt-6-sol pane=3e309d9d turn=fc8daae437874e24b11a016cd98ce58c/18198267bb14475c9a59e60a1a807e5d -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["C3Q2", "EE11"], "githu… → {"plans": [], "commits": ["39b82175e4dc3f20795cbeb2c8106a9d33bd595d"], "evidence…

<!-- relay:entry 20260925T021153Z-az author=agent kind=note model=claude-opus-5-5 pane=983a6a3c turn=92776ab6493d4adca6ab798cb400383d/7c4f7706508043cd9f8c4a3c66fb298f -->
Example requested by the owner (2026-09-25): #0FBB, a UI change verified from a screenshot and then refused a close.

- The implementing agent staged Relay under Xvfb, drove it, and captured the header (`#K7Q2 (2)` before the title, no `[n]` badge) and the click-open list of claims (docs/qa_evidence/2026-09-25-0FBB/01-header-chip.png, 02-chip-menu.png). It read those images itself in the same turn.
- The owner replied in chat: "that screenshot verifies it for me".
- The agent recorded that as a `decision` comment and a `## Verdict`, then called `board_move_card` to `done`. The Board refused: `requires: user_close` ("Verification: ask me before closing any card"). The card is still in needs-verification, waiting for a click the owner had already given in words.

It shows two separate gaps this card names:
1. **Owner-authorized chat close.** An explicit "this verifies it" about a named card, after its evidence was shown, should be allowed to close it, with the quote, revision and actor recorded. Under `ask` today it cannot.
2. **Agent-visual verification.** For a nameable visual claim ("the code sits before the title", "a count in parens", "the badge is gone"), the screenshot is the evidence, and a model reading it can check each `Done means` line. The `ai-visual` rung is currently always demoted to `also` (`ai_may_gate_after: never`). A verifier outside the author's lineage reading the images could have gated this card. Subjective judgements such as "looks good" would still go to a person.

<!-- relay:entry 20260925T041028Z-89 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T041028Z-8a author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Discussing → Planning · a Plan turn started

<!-- relay:entry 20260925T165339Z-ke author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T165339Z-kf author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planning · a Plan turn started

<!-- relay:entry 20260925T165711Z-0y author=agent kind=event model=glm-5.3 pane=switchboard turn=b7f48fd1f94f01ca5b57ebc58199a9f3/0f22ecf3db804d3aba6c70cd5c2d45c1 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T165728Z-73 author=agent kind=event model=glm-5.3 pane=switchboard turn=b7f48fd1f94f01ca5b57ebc58199a9f3/0f22ecf3db804d3aba6c70cd5c2d45c1 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T165736Z-1z author=agent kind=comment mode=plan model=glm-5.3 turn=b7f48fd1f94f01ca5b57ebc58199a9f3/0f22ecf3db804d3aba6c70cd5c2d45c1 -->
I'll plan card #P7CF. Reading the card, its related cards, and the code it names first.

Card and related cards read. Now the code: `qa_policy.py` and the gates in `board_tools.py`.

This Plan turn is one structured ask (read, then two section writes), so I'll continue without a todo list. Reading the gates in `board_tools.py`.

I have the gates. Two more look-ups so the plan names real functions: how thread comments are appended and what `OWNER_ACTOR` is.



One last check so the plan names real files: the docs that describe the policy and the tests that cover it.

I have everything: the gates (`_qa_ask_gate`, `_verify_gate`, `_human_qa_gate` in `board_tools.py`), the floor rules in `qa_policy.py`, the tests (`tests/test_qa_policy.py`, `tests/test_board_tools.py`), and the docs pages. Writing `## Done means` first, then the plan.

Need the card's current hash — re-reading.



`## Done means` written. Now the plan itself.

Planned on #P7CF: the plan turns the research into four changes — an owner-authorized chat close (`authorized_by` on `board_move_card`, quote and relayer recorded on the thread, `ask` otherwise untouched), the person-gate satisfied by that recorded authorization, `ai-visual` allowed to gate when the verifier is outside the implementer's lineage, and a read-only backlog manifest with a pilot sample that moves no card.

One choice is yours before Run: whether the `ai-visual` relaxation should be unconditional (as planned) or behind a new policy key like `ai_visual_may_gate:` — it loosens a default #C3Q2 set on your steer.

<!-- relay:entry 20260925T165736Z-2x author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260926T050221Z-2c author=agent kind=event mention=SWQN model=gpt-6-sol pane=dc713c52 turn=54948a7060344df2abef877a514ad9bf/77d64610668645e0819475f4bc9fcb8b -->
mentioned in #SWQN · 2026-09-26 · agent

<!-- relay:entry 20260926T050307Z-bj author=agent kind=decision model=gpt-6-sol pane=dc713c52 turn=54948a7060344df2abef877a514ad9bf/77d64610668645e0819475f4bc9fcb8b -->
Owner: "yes to all." For #YZ8G question 4, include planted defects and clean controls in #P7CF’s stratified AI-verifier pilot. This is a scope note for the existing owner/session; no implementation is taken over here.
