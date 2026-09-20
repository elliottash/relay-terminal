<!-- relay:entry 20260920T114449Z-m4 author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-20-command-not-found-bug.md

<!-- relay:entry 20260920T114553Z-dj author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260920T114553Z-jj author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
command not found bug
```

</details>

<details><summary>after</summary>

```
when i type agent prompts, the terminal is still often saying "command not found". here is an example:

✦ another session like that, same issue?

19c1ca7ec71d47019abe9aa536469582
command not found: another

session: 798d406f5d60481a89cd76b2c0ac7b0c

thats a bug
```

</details>

<!-- relay:entry 20260920T114557Z-k1 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T114557Z-k2 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260920T115512Z-s4 author=agent kind=event model=glm-5.3 pane=switchboard turn=7ae58a0e8a4044009735af5fd7952d03/94272045165e4bd980c8bd08304154b6 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T115528Z-8c author=agent kind=comment mode=plan model=glm-5.3 turn=7ae58a0e8a4044009735af5fd7952d03/94272045165e4bd980c8bd08304154b6 -->
The router (Python, worker side) already routes that sentence to the agent — so the note is probably printed by the GUI side regardless. Let me find the printing site.









The dispatch function in `src/Pane.h:9509` is the printing site. The file is too big for one read; let me pull its region with targeted searches.











`prompt.why` printed at `src/Pane.h:9230` is the note. Now I need the exact dispatch body; let me check the tests and docs that pin its behavior.









The line numbers shifted between searches — the file may be changing. Let me re-check the current positions.









The auto path looks correctly gated for this exact line — but there are other `submitAgent` callers that pass a `why`. Let me check the one at old line 10000 and how the route reply reaches dispatch.







The GUI side has two candidate holes. Let me check the worker's route handler to see when route_assist runs and what the routed event carries.



The worker's route reply is a plain pass-through of `classify()`. Let me confirm what HEAD's tests already cover for this shape of line.

I have the full picture now. Writing the plan to the card.

Plan written to #EB4A. The note is the router's `invalid_reason` printed as `prompt.why` under the ✦ echo; at HEAD the owner's exact line is already suppressed in auto mode, so the plan reproduces that first, then closes two latent gaps that can still print it (the forced-agent `Decision` shipping `explain_invalid: true` by default, and an ungated `submitAgent(text, true, problem)` in `dispatch()`'s shell branch), plus regression tests with the owner's line.

One thing from the owner: the Risks section asks whether that pane was running a build older than the 2026-09-18 fixes — worth rebuilding and retyping the line before Execute, though the hardening is worth landing either way.

<!-- relay:entry 20260920T115528Z-rc author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260920T212453Z-by author=agent kind=event model=kimi-k3 pane=b2bc87a9 turn=cd14e3411c104989b6ae99393507dcfd/87603178ea23404fb0df731d160403b3 -->
- ✦ agent claimed this card · assignee agent, Planned → Executing, implemented_by kimi/kimi-k3, session b2bc87a9

<!-- relay:entry 20260920T212453Z-z5 author=agent kind=progress model=kimi-k3 pane=b2bc87a9 turn=cd14e3411c104989b6ae99393507dcfd/87603178ea23404fb0df731d160403b3 pane_token=b2bc87a9-d7f0-49f9-a9d9-15e6b0768418 -->
Claimed (b2bc87a9) · working on it from a terminal pane

Executing the plan on the card: reproduce at HEAD, then harden the forced-agent explain_invalid default in router.py and gate dispatch()'s shell-branch note, plus regression tests with the owner's line.

<!-- relay:entry 20260920T214642Z-ze author=agent kind=event model=kimi-k3 pane=b2bc87a9 turn=cd14e3411c104989b6ae99393507dcfd/87603178ea23404fb0df731d160403b3 -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260920T214701Z-zr author=agent kind=event model=kimi-k3 pane=b2bc87a9 turn=cd14e3411c104989b6ae99393507dcfd/87603178ea23404fb0df731d160403b3 -->
- ✦ agent updated this card · appended to `## QA checklist`

<!-- relay:entry 20260920T214816Z-7a author=agent kind=event model=kimi-k3 pane=b2bc87a9 turn=cd14e3411c104989b6ae99393507dcfd/87603178ea23404fb0df731d160403b3 -->
- ✦ agent moved this card · Executing → Needs verification · Forced-agent decisions now judge explain_invalid like auto mode, and dispatch()'s shell branch gates the note on it; 44 router tests pass and the Xvfb live check shows prose quiet and gti status explained. · evidence docs/qa_evidence/2026-09-20-command-not-found-agent-prompts/ · implemented_by kimi/kimi-k3
