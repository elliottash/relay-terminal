<!-- relay:entry 20260925T015527Z-zk author=agent kind=event model=glm-5.3 pane=b4bd3aae turn=9d0bc61f73af46c091f90c8037f21562/eababd827837455cb63a138ac6d70964 -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-24-test-compact-resume-recap-and-plan-execute-fails.md

<!-- relay:entry 20260925T052713Z-hq author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T052713Z-hr author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260925T052917Z-tf author=agent kind=event model=glm-5.3 pane=switchboard turn=d138c02e4f489ec50102fdc85442008a/30df832f814c45069fb647c0579e11ce -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T052934Z-mw author=agent kind=event model=glm-5.3 pane=switchboard turn=d138c02e4f489ec50102fdc85442008a/30df832f814c45069fb647c0579e11ce -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T052941Z-x5 author=agent kind=comment mode=plan model=glm-5.3 turn=d138c02e4f489ec50102fdc85442008a/30df832f814c45069fb647c0579e11ce -->
I'll plan this card. First, reading the card and finding the failing test.

There's a related card from the same day — let me check whether it's a duplicate, and read the test itself.

This is a Plan turn with two writes at the end — no todo list needed. I found a near-duplicate planned card (#P4XN); now let me read the current code state before writing.

Code state confirmed: `execution_prompt` now opens `Run the plan in {path}:`. Let me check the `_plan_execute` caller and any other tests asserting the old wording.

`tests/test_sessions.py:751` already asserts the new wording with a comment citing card #BGRN — that's the deciding evidence. Let me read that card.

One more check — the older failure quoted a prompt opening `# Plan Restore Plan`; let me see where that comes from so the plan is accurate about the current failure.

The picture is complete and the archaeology is settled by evidence already in the tree: `tests/test_sessions.py:751` asserts the new `Run the plan in …:` opening with a comment citing #BGRN's Execute→Run terminology sweep — the sweep simply missed `tests/test_session_protocol.py:347`. Writing `## Done means` first.

Now the plan itself.

Planned #BCJF: the code is right and the test is stale — `tests/test_sessions.py:751` proves on main that `execution_prompt` deliberately opens `Run the plan in <path>:` (#BGRN's Execute→Run sweep), and the sweep simply missed `tests/test_session_protocol.py:347`, so the plan is a one-line test fix plus folding the duplicate #P4XN into this card. Nothing needs deciding: the only open question in #P4XN (deliberate vs regression) is settled by that passing sibling test, and the plan carries a guard step in case a clean export shows anything unexpected. Ready to Run.

<!-- relay:entry 20260925T052941Z-x6 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260925T171555Z-09 author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Planned → Executing, session 33661be1

<!-- relay:entry 20260925T171555Z-28 author=owner kind=progress pane=switchboard pane_token=33661be1-24ff-494d-85df-72b15a2410ad -->
Claimed (33661be1) · working on it from a terminal pane

<!-- relay:entry 20260925T171725Z-tt author=agent kind=event model=claude-opus-5-5 pane=33661be1 turn=8dd1ecab2ef5499ca9b35c590b9d82a4/040f3b13420547d28c880609acf79731 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"plans": [], "commits": ["9f9584ae", "9d31259a"], "evidence": ["docs/qa_evidenc…; appended to `## Execution Summary`

<!-- relay:entry 20260925T171730Z-tz author=agent kind=event model=claude-opus-5-5 pane=33661be1 turn=8dd1ecab2ef5499ca9b35c590b9d82a4/040f3b13420547d28c880609acf79731 -->
- ✦ agent moved this card · Executing → Needs verification · Stale assertion updated to #BGRN's "Run the plan in" wording (9f9584ae); passes on a clean export of the landed sha. · evidence docs/qa_evidence/2026-09-25-bcjf-plan-execute-test-wording/ · implemented_by anthropic/claude-opus-5-5 via claude-code
