<!-- relay:entry 20260919T233143Z-wf author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-dont-require-that-new-switchboard-sections-colle.md

<!-- relay:entry 20260919T233200Z-8q author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260919T233200Z-ha author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
dont require that new switchboard sections collect something.
```

</details>

<details><summary>after</summary>

```
dont require that new switchboard sections collect something. eg i want to create a research section, i can put things in there manually.
```

</details>

<!-- relay:entry 20260919T233518Z-jh author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · title: dont require that new switchboard sections collect something. → improve switchboard sections; replaced `## Issue`

<!-- relay:entry 20260919T233518Z-ji author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote title

<details><summary>before</summary>

```
dont require that new switchboard sections collect something.
```

</details>

<details><summary>after</summary>

```
improve switchboard sections
```

</details>

<!-- relay:entry 20260919T233518Z-rw author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
dont require that new switchboard sections collect something. eg i want to create a research section, i can put things in there manually.
```

</details>

<details><summary>after</summary>

```
dont require that new switchboard sections collect something. eg i want to create a research section, i can put things in there manually.

make these default switchboard sections:

inbox

discussing 

planning

ready

executing

needs verification

needs QA

done
```

</details>

<!-- relay:entry 20260919T233732Z-7v author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260919T233732Z-xc author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
dont require that new switchboard sections collect something. eg i want to create a research section, i can put things in there manually.

make these default switchboard sections:

inbox

discussing 

planning

ready

executing

needs verification

needs QA

done
```

</details>

<details><summary>after</summary>

```
dont require that new switchboard sections collect something. eg i want to create a research section, i can put things in there manually.

make these default switchboard sections:

inbox

discussing 

planning

planned

executing

needs verification

needs QA

done

make these section moves deterministic based on stage. inbox on entry. discussing on first thread entry. planning when planning activated. planned when plan is ready.  executing after you hit execute, but before the agent says its done. the agent then moves to needs verification. after verification, moves to needs qa or back to an earlier stage if needed.: discussing, planning, planned., execuiting.
```

</details>

<!-- relay:entry 20260919T233738Z-1j author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T022931Z-wa author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T023754Z-xt author=agent kind=event model=glm-5.3 pane=switchboard turn=95b4e67cb7e148d4a38d9e5e49cbc565/5e9e31f53ad54ac39b616a334e99e84b -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T023803Z-6d author=agent kind=comment mode=plan model=glm-5.3 turn=95b4e67cb7e148d4a38d9e5e49cbc565/5e9e31f53ad54ac39b616a334e99e84b -->
I have the full picture of how sections, statuses and the plan flow work today. Writing the plan to the card now.

**Done:** the plan is on #3XZV — manual sections via a new `section:` field (nothing has to collect a status any more), the eight stage sections as defaults with the moves made by Relay itself at each stage event (first thread entry, plan start, plan written, Execute, land, verify), plus this repo's own board migration.

It needs three decisions from you before Execute, listed in the plan's Risks: whether needs-QA after verification is the existing cross-model QA run, whether the one `executing` section may collect plan cards too, and whether a card parked in a manual section stays there through stage moves.

<!-- relay:entry 20260920T024259Z-02 author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260920T024300Z-ex author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → In progress · Execute: handed to a terminal pane

<!-- relay:entry 20260920T024300Z-rc author=owner kind=progress pane=switchboard -->
Execute · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260920T031557Z-0s author=agent kind=event model=glm-5.3 pane=6a4eefda turn=f4c62ab8e34947aebe8d463e3838fe67/f37d755e3c1e49af81f48729cf670477 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"commits": ["4f5acd43"], "evidence": ["docs/qa_evidence/2026-09-20-switchboard-…; replaced `## QA checklist`

<!-- relay:entry 20260920T031603Z-qd author=agent kind=event model=glm-5.3 pane=6a4eefda turn=f4c62ab8e34947aebe8d463e3838fe67/f37d755e3c1e49af81f48729cf670477 -->
- ✦ agent moved this card · Executing → Needs QA (LLM) · Landed in 4f5acd43 (board.py's part in 81ff60f8): manual sections, the stage lifecycle, deterministic stage moves, and this board's migration. Build green, worker smoke-checked; no test suites per the owner's instruction. · evidence docs/qa_evidence/2026-09-20-switchboard-sections/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260920T031617Z-ye author=agent kind=progress model=glm-5.3 pane=6a4eefda turn=f4c62ab8e34947aebe8d463e3838fe67/f37d755e3c1e49af81f48729cf670477 -->
Landed and moved to needs QA. Commit 4f5acd43 (34 files, Implemented-By: glm/glm-5.3) carries the worker, GUI, docs and this board's migration; board.py's statuses and `section` field are on main inside 81ff60f8, swept there by the #VKFV session's land while uncommitted. Three cards were left out of the migration on purpose — #EH98's word-wrap (already in a QA lane) and #BRD3/#8YQ9 (live-executing in another pane's worktree); their ready/in-progress → planned/executing rename rides their own sessions' lands. Evidence: docs/qa_evidence/2026-09-20-switchboard-sections/. Pre-existing and not mine: `bad_id BVL1` (the bevel-stylesheet card's id is not Crockford base32, referenced by two other cards) — filed separately.

<!-- relay:entry 20260920T031656Z-b8 author=agent kind=event model=glm-5.3 pane=6a4eefda turn=f4c62ab8e34947aebe8d463e3838fe67/f37d755e3c1e49af81f48729cf670477 -->
- ✦ agent updated this card · links: {"commits": ["4f5acd43"], "evidence": ["docs/qa_evidence/2026-09-20-switchboard-… → {"commits": ["4f5acd43", "d7aebe6f"], "evidence": ["docs/qa_evidence/2026-09-20-…

<!-- relay:entry 20260920T040425Z-8w author=agent kind=progress model=anthropic/claude-opus-5 pane=relay-terminal-2c -->
The four tests that failed on a clean HEAD export after 4f5acd43 are fixed and landed in `cfe58e2c`: board-model is 76 green, boardsections 19 green. All four were assertions this card's change left behind, and no behaviour was touched.

- `executeHandsTheCardToAPaneAndMovesItToInProgress` → `...ToExecuting`: the stage is `executing` now, and the old name was the assertion.
- `theExecuteTaskCarriesTheBoardsConventions`: the brief no longer names `needs-qa-llm`; it says needs-verification with the evidence and a `## QA checklist`, and that the verifier moves it on to QA.
- boardsections `aNewSectionNeedsANameAndStatusesNobodyElseCollects`: no statuses is a manual section now, so it is accepted rather than refused.
- `theVerifyTaskIsTheQaChecklistAndAsksForTheVerifiedByTrailer`: this one is worth a second look by whoever owns `verifyTask`. The new `status` argument sits **before** `note`, and `note` has a default — so every old five-argument call still compiles with the owner's note landing in `status`, and builds a brief with no owner note at all. The test was checking a brief nobody would ever be handed. I passed the status by name here and asserted the lane sentence it decides, but any other five-argument caller of `verifyTask` has the same silent bug.

Found by the #8YQ9 session (relay-terminal-a9) against a clean export; it and I are different sessions, and neither owns this card — recorded here because the cause is this one.

<!-- relay:entry 20260920T040434Z-7c author=agent kind=progress model=anthropic/claude-opus-5 pane=relay-terminal-2c -->
Checked the `verifyTask` callers so nobody has to: there is no other one to fix. `BoardPane.cpp:4674` passes all six (`m_detail->status(), note`), and the only other test caller passes four and asserts text that does not depend on the lane. The trap was real but its blast radius was the one test.
