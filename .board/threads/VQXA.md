<!-- relay:entry 20260925T011543Z-cj author=agent kind=event model=claude-opus-5-5 pane=d7900abc turn=ac598e144c144e34a442ba28dd6ed4c3/af28e942d2e34407a8240d18aa090b52 -->
- ✦ agent created this card in Planned · .board/changes/2026-09-24-todo-reminders-fire-every-8-steps-on-a-single-in.md

<!-- relay:entry 20260925T011552Z-ty author=agent kind=event model=claude-opus-5-5 pane=d7900abc turn=ac598e144c144e34a442ba28dd6ed4c3/af28e942d2e34407a8240d18aa090b52 -->
- ✦ agent updated this card · replaced `## Planning notes`

<!-- relay:entry 20260925T040043Z-jv author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T040043Z-m5 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planned → Planning · a Plan turn started

<!-- relay:entry 20260925T040348Z-b9 author=agent kind=event model=glm-5.3 pane=switchboard turn=5aa3a2a03988ce3436be59ca7cc8c949/3971235cafa34196942d2c086a15c678 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T040401Z-d7 author=agent kind=event model=glm-5.3 pane=switchboard turn=5aa3a2a03988ce3436be59ca7cc8c949/3971235cafa34196942d2c086a15c678 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T040407Z-p3 author=agent kind=comment mode=plan model=glm-5.3 turn=5aa3a2a03988ce3436be59ca7cc8c949/3971235cafa34196942d2c086a15c678 -->
I'll read the card for its hash, and start finding the code the card names.

Now the code: the staleness reminder in `todos.py` and the "keep working" recap in `agent.py`.

That reminder is the very behaviour this card is about — a single focused ask needs no list. Continuing the read: where `agent.py` fires both reminders.

`agent.py` is too big to read whole — I'll read the two regions that matter: the injection site near line 2591 and the end-of-turn text near 3981.

`read_file` refuses `agent.py` outright (over its 128 KiB limit), and a Plan turn may not run commands — so I'll reconstruct the two regions with `search_files` patterns.

Good — that locates the machinery. Now the trigger conditions, the end-of-turn text at 3981, and existing test coverage.

One more pass to identify the function holding the 3981 "keep working" note, since that's the ~10× offender.



The ~10× offender is `_recitation` (agent.py:3964) — the periodic recital that appends "Keep working through the open items above". Now the cadence constants, what user-visible events exist to gate it on, and the logging hook.

`_recitation` is the ~10× offender — injected on a pure timer (`RECITE_STEPS = 25` / `RECITE_TOOL_CALLS = 50`, agent.py:2604). One last read: how the existing reminder tests drive the loop, so the plan's test step is grounded.

I have everything: the injection sites, the cadence constants, the gating hooks, the log pattern, and — importantly — `test_stale_todo_reminder` (tests/test_requests.py:402) which asserts the exact single-task reminder this card removes. Writing `## Done means` first.

Now the plan.

**Plan written on #VQXA.** It gates the two offenders — the 8-step staleness reminder now fires only when a `pending` todo exists (`todos.py` predicate, applied at `agent.py:2589`), and the "Keep working…" recital only after a real event (compaction, steer, or a >30s tool wait) — plus a `reminder_injected` log line at all four injection sites, with tests beside the existing `Script`-harness reminder cases. One thing needs you: the plan picks the simpler "any pending item" rule over the card's touch-area heuristic, and accepts that drift-looping turns lose the recital entirely (loopdetect still guards) — both flagged as Risks on the card if you want them otherwise.

<!-- relay:entry 20260925T040408Z-x9 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260925T215145Z-4q author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Planned → Running, session 6ccc5d01

<!-- relay:entry 20260925T215145Z-zt author=owner kind=progress pane=switchboard pane_token=6ccc5d01-a1b3-4a97-b82c-aec0a9594eb5 -->
Claimed (6ccc5d01) · working on it from a terminal pane

<!-- relay:entry 20260925T215611Z-7z author=agent kind=event model=claude-opus-5-5 pane=6ccc5d01 turn=5853c4498cbd4b2698c94908a171eb83/52bc74a4b37c4c959151a72473cf315d -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["6747a8043db8", "89de054cf66d"], "evidence": [], "rela… → {"plans": [], "commits": ["6747a8043db8", "89de054cf66d"], "evidence": ["docs/qa…; appended to `## Execution Summary`

<!-- relay:entry 20260925T215620Z-q8 author=agent kind=event model=claude-opus-5-5 pane=6ccc5d01 turn=5853c4498cbd4b2698c94908a171eb83/52bc74a4b37c4c959151a72473cf315d -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260925T215621Z-6q author=agent kind=event model=claude-opus-5-5 pane=6ccc5d01 turn=5853c4498cbd4b2698c94908a171eb83/52bc74a4b37c4c959151a72473cf315d -->
- ✦ agent moved this card · Running → Needs verification · Landed in 6747a804 + 89de054c; 110 targeted tests pass on a clean export of HEAD · evidence docs/qa_evidence/2026-09-25-todo-reminders-gated/ · implemented_by anthropic/claude-opus-5-5 via claude-code

<!-- relay:entry 20260925T215624Z-ne author=agent kind=progress model=claude-opus-5-5 pane=6ccc5d01 turn=5853c4498cbd4b2698c94908a171eb83/52bc74a4b37c4c959151a72473cf315d -->
Landed `6747a804` + `89de054c`; moved to needs-verification. The staleness note now needs a `pending` todo. The recital also needs a steer, a takeover, a compaction that rewrote the transcript, or a tool wait of 30 s or more. All four reminder sites log `reminder_injected`. 110 targeted tests pass on a clean export. Also: `land.py` had added `6747a804` to #234Z's links.commits because the message cites #234Z as evidence; I removed it there.
