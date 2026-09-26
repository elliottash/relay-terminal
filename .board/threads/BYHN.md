<!-- relay:entry 20260925T220308Z-sz author=agent kind=event model=claude-opus-5-5 pane=dd2f0e10 turn=394690d161c344479ecbca42d7f935b4/eee1f549a32a4b419681f4ef03528400 -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-25-short-profile-board-tool-list-is-335-bytes-over.md

<!-- relay:entry 20260925T220317Z-nt author=agent kind=event mention=K54A model=claude-opus-5-5 pane=dd2f0e10 turn=394690d161c344479ecbca42d7f935b4/eee1f549a32a4b419681f4ef03528400 -->
mentioned in #K54A · 2026-09-25 · agent

<!-- relay:entry 20260925T223648Z-d0 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T223649Z-py author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260925T224220Z-0k author=agent kind=event model=glm-5.3 pane=switchboard turn=55b5f1933800fc34299d3ef05c9b05e3/5d68172c2e464a92bc05f284a9fda529 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T224236Z-e1 author=agent kind=event model=glm-5.3 pane=switchboard turn=55b5f1933800fc34299d3ef05c9b05e3/5d68172c2e464a92bc05f284a9fda529 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T224241Z-pg author=agent kind=question model=glm-5.3 pane=switchboard turn=55b5f1933800fc34299d3ef05c9b05e3/5d68172c2e464a92bc05f284a9fda529 -->
Raise the short-profile tool budget 11 → 12 KiB, or hold 11 KiB? The five board tools legitimately grew +1,152 B since the budget was measured on 2026-09-20 (#MJ76 metadata, #EE42 links, #ZB9M metering); I measured the honest no-rule-lost trim at only ~250 B, short of the 335 needed. **Recommendation: raise to 12 KiB** — holding 11 KiB forces cutting board_list's case-ledger args (~500 B) from every profile, a feature removal. Answer here and Run picks it up (plan step 1 vs step 4).

<!-- relay:entry 20260925T224245Z-mz author=agent kind=comment mode=plan model=glm-5.3 turn=55b5f1933800fc34299d3ef05c9b05e3/5d68172c2e464a92bc05f284a9fda529 -->
I'll read the card and its related cards first, then the code.

Now the code: the failing test and the tool specs it measures.

This is one ask (plan the card), so no todo list. Reading the test and the specs now.



The test pins two things: the 11 KiB budget, and that the five board tools are sent **byte for byte** as the full profile sends them (no short descriptions). Now the specs themselves and who claims the file.





Three live sessions claim `board_tools.py` right now (pane `ca66ac8d` 27 m, `fyey-py` 2 h13 m, `relay-mj76` 4 h39 m) — that shapes the plan's risk block. Now let me measure exactly where the 335 bytes sit.

The test fails on the working tree too; the five board specs are 7,111 B of the 11,599 B list. Note `board_list` here is 1,703 B against the card's 1,584 B at HEAD — another session's uncommitted edit. Now `prompt_profiles.py` and what the descriptions were when the budget was set.

Some description text is pinned by tests (`board_claimed_elsewhere` must stay in `board_claim`'s description). Now the exact growth since the budget was set at `cd356730`:

That approach was clumsy. Simpler: exec just the `spec(...)` blocks with the module's own `spec` helper.













The five grew +1,152 B since the 10.2 KB baseline: `board_list` +529, `board_create_card` +637. Let me see the exact text that grew, to name concrete trims.

The texts are clear. Two checks before writing: what `test_prompt_profiles.py` pins about these five, and the `spec()` helper's fixed overhead.

`additionalProperties: False` is structural, `_ID_ARG` is already shared, and only `board_claimed_elsewhere` is pinned. Last look: the full `labels` text — the biggest single arg.

Drafting the tightened texts and measuring real savings so the plan can name exact trims:

Prose tightening alone yields ~190 B of the needed 335. Let me check the remaining two tools for redundancy before concluding.

One more check — the exact companion tests that pin the byte-for-byte rule, so the plan's Verify section names them.



I have everything I need. Writing `## Done means` first, then the plan.

The write needs the card's hash — re-reading the card.



`## Done means` is in. Now the plan.

Plan is on the card. One genuine owner decision remains — posting it as a question.

**Done:** #BYHN now carries its `## Done means` and a measured `## Plan` — trim ~250 B of prose fat from the five board specs in `board_tools.py` (board_create_card description/labels/summary, board_claim, `cases`, `_ID_ARG`), keep every decision-8 rule, and land through `land.py` with the contested-file confirm review.

What it needs from you: the plan's trim alone reaches only ~250 B of the 335 needed — the rest of the growth is real feature work — so I asked on the card whether to **raise the budget to 12 KiB** (my recommendation) or hold 11 KiB and cut board_list's case-ledger args instead.

<!-- relay:entry 20260925T224245Z-n0 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
