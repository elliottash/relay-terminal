<!-- relay:entry 20260925T135952Z-1p author=agent kind=event model=glm-5.3 pane=cca5043b turn=2b987b9a9bc5455289c56c2c373290f3/bd856a2ad54e4e9aa55e26b68904ff7a -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-25-bundled-disk-hygiene-description-is-153-chars-th.md

<!-- relay:entry 20260925T140004Z-3k author=agent kind=event model=glm-5.3 pane=cca5043b turn=2b987b9a9bc5455289c56c2c373290f3/bd856a2ad54e4e9aa55e26b68904ff7a -->
- ✦ agent updated this card · appended to `## Issue`

<!-- relay:entry 20260925T141450Z-r5 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T141451Z-9t author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260925T141732Z-fg author=agent kind=event model=glm-5.3 pane=switchboard turn=7f209a05a13f4b35f568729448a8ce7d/7e02f923e84e49a28285ffca1d81de94 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T141801Z-nr author=agent kind=event model=glm-5.3 pane=switchboard turn=7f209a05a13f4b35f568729448a8ce7d/7e02f923e84e49a28285ffca1d81de94 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T141807Z-63 author=agent kind=comment mode=plan model=glm-5.3 turn=7f209a05a13f4b35f568729448a8ce7d/7e02f923e84e49a28285ffca1d81de94 -->
Reading the card and the code it names.

Now the skill file, the validator, and the failing test.

This is a single planning ask, so no todo list. Two more reads to ground the fix choice — the test itself, and how long the other bundled descriptions are.

`run_command` is refused in a Plan turn — reading instead. Checking every bundled description's length via search, so the plan picks the right fix.

One more check — that this description text isn't duplicated anywhere else (packaging, docs) that a trim would also have to touch.

The picture is complete: `MAX_DESCRIPTION` is read by nothing at runtime — only by the test — the skill's `short:` line (79 chars) is what the catalogue shows, and disk-hygiene is the only bundled skill over the bound (deliver 140, guest-account-setup 135, local-model-setup 142). Writing `## Done means`, then the plan.



`## Done means` is in. Now the plan.

**Planned #EHBH:** the fix is a one-line trim of the disk-hygiene `description:` to ≤ 150 chars (a 144-char replacement is proposed on the card), landed via `scripts/land.py` and verified with `PYTHONPATH=backend python3 -m unittest tests.test_skills` — investigation confirmed `MAX_DESCRIPTION` is enforced only by that test, nothing at runtime, and the skill's `short:` line (79 chars) is what the agent actually sees in the catalogue.

The one decision left to you, flagged in the plan's Risks: trim (recommended — every other bundled skill fits the bound) or keep the 153-char sentence and raise `MAX_DESCRIPTION` instead. Say the word if it's the latter; otherwise Run can go ahead as written.

<!-- relay:entry 20260925T141807Z-d5 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
