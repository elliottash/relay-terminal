<!-- relay:entry 20260920T024135Z-sx author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-in-plan-mode-the-terminal-agent-or-switchboard-a.md

<!-- relay:entry 20260920T024150Z-dn author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T024628Z-cx author=agent kind=event model=glm-5.3 pane=switchboard turn=b5491bb8048c42be9ba7746dc8652874/c5e7978d4ee941bf9d4b2adaf6b79479 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T024635Z-dt author=agent kind=comment mode=plan model=glm-5.3 turn=b5491bb8048c42be9ba7746dc8652874/c5e7978d4ee941bf9d4b2adaf6b79479 -->
Now let me explore the codebase for plan mode and orchestration.





































































I have the full picture now. Writing the plan to the card.

**Plan written to #K3TY.** It is a prompt-level change: both plan-mode prompts (the pane's `PLAN_MODE_NOTE` and the card Plan brief, bumped to v2) learn to write an **Orchestration** block into plans worth splitting, and both Execute prompts (`planning.execution_prompt`, `board::executeTask`) tell the executing agent to follow it — Warp-style, with Execute as the approval, no new protocol messages.

It needs one decision from the owner (in the plan's Risks): whether plan-mode turns should also be allowed to *use* read-only `explore` subagents while investigating — my recommendation is no, keep that a separate card.

<!-- relay:entry 20260920T041037Z-0q author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +1

<!-- relay:entry 20260920T041037Z-cw author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +2

<!-- relay:entry 20260920T041319Z-72 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T041319Z-73 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260920T041455Z-63 author=agent kind=event model=glm-5.3 pane=switchboard turn=05598b24d6d048f3a4545bddd3573d22/64b786ab26c74156b50248257d3a5a3a -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T041459Z-2p author=agent kind=question model=glm-5.3 pane=switchboard turn=05598b24d6d048f3a4545bddd3573d22/64b786ab26c74156b50248257d3a5a3a -->
1. Should plan-mode turns also be allowed to *use* read-only `explore` subagents while investigating, instead of today's single-agent plan mode (`agent.py:647`)? Recommendation: no — this card is about planning the orchestration, not running agents in plan mode; if wanted, file it as its own card after this lands.

<!-- relay:entry 20260920T041503Z-3x author=agent kind=comment mode=plan model=glm-5.3 turn=05598b24d6d048f3a4545bddd3573d22/64b786ab26c74156b50248257d3a5a3a -->
I have the card's current state and the three core files. The existing plan's findings check out so far, with one citation to correct (`executeTask` sits at `BoardModel.cpp:361`, not 348). Let me verify the remaining citations before revising.

The findings mostly verify. Two citations need checking: `Carry out the plan` found no matches in `tests/`, and the brief check in `test_board_protocol.py:891` didn't surface. Let me pin those down, plus the exact plan-mode withholding in `agent.py`.

All findings are now verified — the plan holds, with two stale line citations to correct (`BoardModel.cpp:361`, not 348; the pinned test assertions sit at `2115, 2144-2156, 2302, 2533`). Revising the plan in place:

Plan revised. Now posting the outstanding owner decision as a numbered question so it doesn't stay buried in Risks:

**Plan revised on #K3TY.** I re-verified every citation against the code — the approach held, so the revision only corrects stale line anchors (`executeTask` is `BoardModel.cpp:361`, pinned by `boardmodel_test.cpp:2115/2144-2156/2302/2533`) — and the plan itself is unchanged: a prompt-level change teaching both plan prompts to write an **Orchestration** block and both Execute prompts to follow it.

It needs one decision from you, now posted as question 1 on the card: whether plan mode should also *run* read-only `explore` subagents while investigating — my recommendation is no, keep that separate.

<!-- relay:entry 20260920T041503Z-eg author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260920T042354Z-8k author=owner kind=comment mode=discuss -->
why shouldnt it run read subagents? isnt that what claude does? or should they be read/write?

<!-- relay:entry 20260920T042529Z-d8 author=agent kind=note model=glm-5.3 pane=switchboard turn=6a7c408c8d014c76841f82213b0a2edf/0a81448dbbde48aa9ccdad9ad183837d -->
Answering the owner on question 1 (why not read subagents — isn't that Claude's way? read-only or read/write?):

The "no" was scope-keeping (this card is prompt-level), not a design verdict — and on the merits the owner is right. Claude Code's plan mode keeps read-only tools available and spawning research subagents while planning is a standard pattern there.

Relay's withholding comment (`agent.py:652`, "Subagents may write files, so plan mode does not offer them either") is true only of write-capable definitions like `general`. The built-in `explore` carries `READ_ONLY_TOOLS` (`agents_defs.py:36`: run_command, read_file, list_directory, load_skill, read_skill_file) — no write_file/edit_file — and plan mode's main agent already keeps `run_command` (`PLAN_BLOCKED_TOOLS` is only write_file/edit_file/set_keybinding, `planning.py:13`), so an explore subagent opens no write path the plan turn doesn't already have.

Refined recommendation, replacing question 1's "no":
- Read-only subagent definitions in plan mode: yes, as its own small card. Filter plan mode's subagent specs to read-only definitions (the catalog already carries `read_only` and the tool tuple), and un-gate the two spots that test `mode != "plan"` (`agent.py:653` tool filter, `:1376-1377` batch start). Not folded into K3TY, which changes prompts only.
- Read/write (`general`) in plan mode: no — plan mode's promise is that nothing changes before Execute is approved; a write-capable subagent would break it through the back door.

<!-- relay:entry 20260920T042542Z-55 author=agent kind=comment mode=discuss model=glm-5.3 turn=6a7c408c8d014c76841f82213b0a2edf/0a81448dbbde48aa9ccdad9ad183837d -->
The code answers this well — let me put the refined answer on the card's thread, since it sharpens question 1.

You're right, and my "no" was scope-keeping, not a design verdict. Conceded on the merits:

- **Claude does allow it** — its plan mode keeps read-only tools, and spawning research subagents while planning is a standard pattern there.
- **Relay's stated reason doesn't hold for `explore`.** The code comment says subagents are withheld because "subagents may write files" — true of `general`, not of `explore`: its toolset is read-only (no `write_file`/`edit_file`), and plan mode's main agent already keeps `run_command` anyway, so an explore subagent adds no write path the turn doesn't already have.
- **Read-only, not read/write.** Plan mode's promise is "nothing changes before Execute"; a write-capable subagent would break that through the back door. Read-only fits it exactly.

Refined recommendation: allow read-only definitions in plan mode — as its **own small card** (two code spots gate on `mode != "plan"`: the tool filter and the subagent batch start, `agent.py:653`/`:1376`), not folded into this prompt-level one. Say the word and I'll file it.

Noted on #K3TY, replacing question 1's recommendation.

<!-- relay:entry 20260920T051701Z-g5 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T051701Z-qm author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planned → Planning · a Plan turn started

<!-- relay:entry 20260920T051758Z-tw author=agent kind=event model=kimi-k3 pane=switchboard turn=0c289fb1c0f34db6a06cf3cca748aa32/02ec4f7d8de5405db1b1079e0443348d -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T051806Z-tb author=agent kind=comment mode=plan model=kimi-k3 turn=0c289fb1c0f34db6a06cf3cca748aa32/02ec4f7d8de5405db1b1079e0443348d -->
Citations still hold. The one thing the plan needs is updating the Risks bullet to the position the thread settled on after the owner's challenge.

**Done:** #K3TY's plan is re-verified and current — the citations all hold (`planning.py:17/94`, `BoardModel.cpp:361`), so the six steps are unchanged. The one revision: the Risks bullet on plan-mode subagents now records the position the thread settled after your pushback — read-only subagents in plan mode: yes, as a separate small card; write-capable: no — instead of the old "recommendation: no."

Nothing else is open. The card is ready to move to Planned; say the word if you want the read-only-subagents card filed now.

<!-- relay:entry 20260920T051806Z-tc author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260920T052205Z-w4 author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260920T052205Z-w5 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planned → Executing · Execute: handed to a terminal pane

<!-- relay:entry 20260920T052205Z-w6 author=owner kind=progress pane=switchboard pane_token=0f758e93-29f0-42f2-99d2-3c012081794b -->
Executing (0f758e93) · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.
<!-- relay:entry 20260920T055100Z-k3 author=agent kind=progress model=glm-5.3 -->
- ✦ agent moved this card · Executing → Needs verification · Implemented in 4a0cb8cd + 6011141c: both plan-mode `write_plan` and the card Plan brief (v2) ask for an Orchestration block when the work is big enough to split, and both Execute prompts — the pane's `execution_prompt` and `executeTask`'s hasPlan variants — say to follow it. · evidence docs/qa_evidence/2026-09-20-plan-orchestration-block/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260920T055100Z-k4 author=agent kind=progress model=glm-5.3 -->
All six plan steps landed: prompts (4a0cb8cd — planning.py, board_plan_brief.md, BoardModel.cpp, three python test files, both docs) and the pinned boardmodel assertions (6011141c, split off only because another session's uncommitted `openCard` helper conflicted in the same file; a snapshot refresh moved it into the merge base and the hunk landed clean). Green on this machine: relay-build, test_questions (35), test_sessions (37), the four executeTask board tests (6 passed). test_board_protocol's 2 failures and the `board` ctest abort both pre-date or bypass this card — each reproduces at pre-card HEAD or lives only in another session's uncommitted tree (evidence README has the lists). Left to the verifier: the live plan→Execute round trip in the QA checklist. The thread-settled read-only-subagents-in-plan-mode card is still unfiled; say the word.
